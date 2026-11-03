// Odposluch: otvorí mikrofón cez bežný zdieľaný režim Windows (takže prejde efektom HearMeOut)
// a pošle ho do predvoleného výstupu. cpal::Stream nie je Send, preto všetko žije vo vlastnom vlákne.
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::mpsc::{channel, Sender};
use std::sync::{Arc, Mutex};

enum Cmd {
    Start { hints: Vec<String>, reply: Sender<Result<String, String>> },
    Stop,
}

pub struct Monitor {
    tx: Mutex<Sender<Cmd>>,
    volume: Arc<AtomicU32>,
    pub running: Arc<AtomicBool>,
}

impl Monitor {
    pub fn new() -> Self {
        let (tx, rx) = channel::<Cmd>();
        let volume = Arc::new(AtomicU32::new(0.8f32.to_bits()));
        let running = Arc::new(AtomicBool::new(false));
        let (vol, run) = (volume.clone(), running.clone());
        std::thread::spawn(move || {
            let mut streams: Option<(cpal::Stream, cpal::Stream)> = None;
            while let Ok(cmd) = rx.recv() {
                streams = None;   // zastav staré
                run.store(false, Ordering::Relaxed);
                if let Cmd::Start { hints, reply } = cmd {
                    match open(&hints, vol.clone()) {
                        Ok((i, o, name)) => { streams = Some((i, o)); run.store(true, Ordering::Relaxed); let _ = reply.send(Ok(name)); }
                        Err(e) => { let _ = reply.send(Err(e)); }
                    }
                }
                let _ = &streams;
            }
        });
        Monitor { tx: Mutex::new(tx), volume, running }
    }

    pub fn start(&self, hints: Vec<String>) -> Result<String, String> {
        let (rtx, rrx) = channel();
        self.tx.lock().unwrap().send(Cmd::Start { hints, reply: rtx }).map_err(|e| e.to_string())?;
        rrx.recv_timeout(std::time::Duration::from_secs(8)).map_err(|_| "Odposluch sa nespustil včas.".to_string())?
    }
    pub fn stop(&self) { let _ = self.tx.lock().unwrap().send(Cmd::Stop); }
    pub fn set_volume(&self, v: f32) { self.volume.store(v.clamp(0.0, 1.5).to_bits(), Ordering::Relaxed); }
}

fn open(hints: &[String], volume: Arc<AtomicU32>) -> Result<(cpal::Stream, cpal::Stream, String), String> {
    let host = cpal::default_host();
    let inputs = host.input_devices().map_err(|e| format!("Neviem načítať mikrofóny: {e}"))?;
    let hints_l: Vec<String> = hints.iter().filter(|h| !h.is_empty()).map(|h| h.to_lowercase()).collect();
    let input = inputs
        .into_iter()
        .find(|d| d.name().map(|n| { let n = n.to_lowercase(); hints_l.iter().all(|h| n.contains(h)) }).unwrap_or(false))
        .ok_or("Nenašiel som mikrofón s efektmi.")?;
    let in_name = input.name().unwrap_or_default();
    let output = host.default_output_device().ok_or("Nenašiel som výstupné zariadenie.")?;

    let in_cfg = input.default_input_config().map_err(|e| e.to_string())?;
    let out_cfg = output.default_output_config().map_err(|e| e.to_string())?;
    let in_ch = in_cfg.channels() as usize;
    let out_ch = out_cfg.channels() as usize;
    let in_rate = in_cfg.sample_rate().0 as f64;
    let out_rate = out_cfg.sample_rate().0 as f64;

    let buf = Arc::new(Mutex::new(VecDeque::<f32>::with_capacity(48000)));
    let target = (in_rate * 0.025) as usize;   // ~25 ms rezerva
    let max_keep = (in_rate * 0.06) as usize;  // nad 60 ms zahadzujeme – odposluch nesmie zaostávať

    // vstup: prvý kanál (APO už dalo mono do všetkých)
    let b_in = buf.clone();
    let push = move |frames: &mut dyn Iterator<Item = f32>| {
        if let Ok(mut q) = b_in.lock() {
            for s in frames { q.push_back(s); }
            while q.len() > max_keep { q.pop_front(); }
        }
    };
    let err = |e| eprintln!("odposluch: {e}");
    let in_stream = match in_cfg.sample_format() {
        cpal::SampleFormat::F32 => input.build_input_stream(&in_cfg.clone().into(),
            { let mut push = push; move |d: &[f32], _: &_| push(&mut d.iter().step_by(in_ch).copied()) }, err, None),
        cpal::SampleFormat::I16 => input.build_input_stream(&in_cfg.clone().into(),
            { let mut push = push; move |d: &[i16], _: &_| push(&mut d.iter().step_by(in_ch).map(|s| *s as f32 / 32768.0)) }, err, None),
        cpal::SampleFormat::I32 => input.build_input_stream(&in_cfg.clone().into(),
            { let mut push = push; move |d: &[i32], _: &_| push(&mut d.iter().step_by(in_ch).map(|s| *s as f32 / 2147483648.0)) }, err, None),
        f => return Err(format!("Nepodporovaný formát mikrofónu: {f:?}")),
    }.map_err(|e| format!("Neviem otvoriť mikrofón: {e}"))?;

    // výstup: lineárny resampling, ak sa frekvencie líšia
    let step = in_rate / out_rate;
    let b_out = buf.clone();
    let mut st = Resampler { a: 0.0, b: 0.0, frac: 0.0, primed: false };
    let mut fill = move |out: &mut dyn FnMut(usize, f32), frames: usize| {
        let vol = f32::from_bits(volume.load(Ordering::Relaxed));
        let mut q = match b_out.lock() { Ok(q) => q, Err(_) => return };
        if !st.primed { if q.len() < target { for i in 0..frames { out(i, 0.0) } return; } st.primed = true; }
        for i in 0..frames {
            while st.frac >= 1.0 {
                st.a = st.b;
                match q.pop_front() { Some(s) => st.b = s, None => { st.primed = false; } }
                st.frac -= 1.0;
            }
            let y = st.a + (st.b - st.a) * st.frac as f32;
            st.frac += step;
            out(i, (y * vol).clamp(-1.0, 1.0));
        }
    };
    let out_stream = match out_cfg.sample_format() {
        cpal::SampleFormat::F32 => output.build_output_stream(&out_cfg.clone().into(),
            move |d: &mut [f32], _: &_| { let n = d.len() / out_ch; fill(&mut |i, y| for c in 0..out_ch { d[i * out_ch + c] = y }, n) }, err, None),
        cpal::SampleFormat::I16 => output.build_output_stream(&out_cfg.clone().into(),
            move |d: &mut [i16], _: &_| { let n = d.len() / out_ch; fill(&mut |i, y| for c in 0..out_ch { d[i * out_ch + c] = (y * 32767.0) as i16 }, n) }, err, None),
        f => return Err(format!("Nepodporovaný formát výstupu: {f:?}")),
    }.map_err(|e| format!("Neviem otvoriť výstup: {e}"))?;

    in_stream.play().map_err(|e| e.to_string())?;
    out_stream.play().map_err(|e| e.to_string())?;
    let out_name = output.name().unwrap_or_default();
    Ok((in_stream, out_stream, format!("{in_name} → {out_name}")))
}

struct Resampler { a: f32, b: f32, frac: f64, primed: bool }
