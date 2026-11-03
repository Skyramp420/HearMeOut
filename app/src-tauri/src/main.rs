// HearMeOut – ovládacia appka (od WheresTheApp)
// Appka nič nespracúva – zvuk rieši APO vo Windows. Appka len píše config, číta meter a spúšťa inštalátor.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::Serialize;
use std::collections::BTreeMap;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tauri::{Emitter, Manager};

#[cfg(windows)]
mod monitor;
mod host;
mod update;

const CLSID: &str = "{6F3B2A9E-4C1D-4E8B-9A57-2D8E0C4F1B63}";
const SECTIONS: &[&str] = &["global", "highpass", "denoise", "gate", "eq", "compressor", "saturation", "deesser", "limiter", "ui"];

type Values = BTreeMap<String, String>;

// ------------------------------------------------------------------ cesty
fn data_dir() -> PathBuf {
    PathBuf::from(std::env::var("ProgramData").unwrap_or_else(|_| "C:\\ProgramData".into())).join("HearMeOut")
}
fn config_path() -> PathBuf { data_dir().join("config.ini") }
fn presets_dir() -> PathBuf {
    PathBuf::from(std::env::var("APPDATA").unwrap_or_default()).join("HearMeOut").join("presets")
}
fn payload_dir() -> PathBuf {
    std::env::current_exe().ok().and_then(|p| p.parent().map(|d| d.join("payload"))).unwrap_or_default()
}

// ------------------------------------------------------------------ INI
fn parse_ini(text: &str) -> Values {
    let mut out = Values::new();
    let mut section = String::new();
    for raw in text.lines() {
        let line = raw.trim().trim_start_matches('\u{feff}');
        if line.is_empty() || line.starts_with(';') || line.starts_with('#') { continue; }
        if line.starts_with('[') && line.ends_with(']') {
            section = line[1..line.len() - 1].trim().to_lowercase();
            continue;
        }
        if let Some((k, v)) = line.split_once('=') {
            let v = v.split(|c| c == ';' || c == '#').next().unwrap_or("").trim();
            out.insert(format!("{}.{}", section, k.trim().to_lowercase()), v.to_string());
        }
    }
    out
}

fn write_ini(values: &Values) -> String {
    let mut s = String::from(
        "; HearMeOut – nastavenie efektov mikrofónu\n; Tento súbor spravuje appka HearMeOut. Môžeš ho upraviť aj ručne – zmena platí hneď.\n",
    );
    let mut sections: Vec<String> = SECTIONS.iter().map(|s| s.to_string()).collect();
    for k in values.keys() {
        if let Some((sec, _)) = k.split_once('.') {
            if !sections.iter().any(|s| s == sec) { sections.push(sec.to_string()); }
        }
    }
    for sec in sections {
        let prefix = format!("{}.", sec);
        let items: Vec<_> = values.iter().filter(|(k, _)| k.starts_with(&prefix)).collect();
        if items.is_empty() { continue; }
        s.push_str(&format!("\n[{}]\n", sec));
        for (k, v) in items { s.push_str(&format!("{} = {}\n", &k[prefix.len()..], v)); }
    }
    s
}

fn write_atomic(path: &PathBuf, text: &str) -> Result<(), String> {
    let tmp = path.with_extension("ini.tmp");
    std::fs::write(&tmp, text).map_err(|e| format!("Neviem zapísať {}: {}", tmp.display(), e))?;
    std::fs::rename(&tmp, path).map_err(|e| format!("Neviem uložiť {}: {}", path.display(), e))
}

// ------------------------------------------------------------------ príkazy: config a presety
#[tauri::command]
fn load_config() -> Values {
    std::fs::read_to_string(config_path()).map(|t| parse_ini(&t)).unwrap_or_default()
}

#[tauri::command]
fn save_config(values: Values) -> Result<(), String> {
    let dir = data_dir();
    if !dir.exists() { return Err("HearMeOut ešte nie je nainštalovaný na mikrofón.".into()); }
    write_atomic(&config_path(), &write_ini(&values))
}

#[derive(Serialize)]
struct Preset { name: String, values: Values }

fn safe_name(name: &str) -> Result<String, String> {
    let n = name.trim();
    if n.is_empty() || n.len() > 60 || n.chars().any(|c| "\\/:*?\"<>|".contains(c) || c.is_control()) {
        return Err("Neplatný názov presetu.".into());
    }
    Ok(n.to_string())
}

#[tauri::command]
fn list_presets() -> Vec<Preset> {
    let mut out = Vec::new();
    if let Ok(rd) = std::fs::read_dir(presets_dir()) {
        for e in rd.flatten() {
            let p = e.path();
            if p.extension().and_then(|x| x.to_str()) != Some("ini") { continue; }
            if let (Some(stem), Ok(t)) = (p.file_stem().and_then(|s| s.to_str()), std::fs::read_to_string(&p)) {
                out.push(Preset { name: stem.to_string(), values: parse_ini(&t) });
            }
        }
    }
    out.sort_by(|a, b| a.name.to_lowercase().cmp(&b.name.to_lowercase()));
    out
}

#[tauri::command]
fn save_preset(name: String, values: Values) -> Result<(), String> {
    let n = safe_name(&name)?;
    let dir = presets_dir();
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    write_atomic(&dir.join(format!("{}.ini", n)), &write_ini(&values))
}

#[tauri::command]
fn delete_preset(name: String) -> Result<(), String> {
    let n = safe_name(&name)?;
    std::fs::remove_file(presets_dir().join(format!("{}.ini", n))).map_err(|e| e.to_string())
}

// ------------------------------------------------------------------ stav systému
#[derive(Serialize)]
struct Endpoint { id: String, name: String, adapter: String, installed: bool }

#[derive(Serialize)]
struct AppState {
    registered: bool,
    endpoints: Vec<Endpoint>,
    data_dir: String,
    payload_ok: bool,
    version: String,
}

fn installed_ids() -> Vec<String> {
    std::fs::read_to_string(data_dir().join("installed-endpoints.txt"))
        .map(|t| t.lines().map(|l| l.trim().to_string()).filter(|l| !l.is_empty()).collect())
        .unwrap_or_default()
}

#[cfg(windows)]
fn system_info() -> (bool, Vec<Endpoint>) {
    use winreg::enums::HKEY_LOCAL_MACHINE;
    use winreg::RegKey;
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
    let registered = hklm.open_subkey(format!("SOFTWARE\\Classes\\CLSID\\{}\\InprocServer32", CLSID)).is_ok();
    let ids = installed_ids();
    let mut eps = Vec::new();
    if let Ok(cap) = hklm.open_subkey("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture") {
        for id in cap.enum_keys().flatten() {
            let Ok(k) = cap.open_subkey(&id) else { continue };
            let state: u32 = k.get_value("DeviceState").unwrap_or(0);
            if state != 1 { continue; }
            let props = k.open_subkey("Properties").ok();
            let get = |n: &str| props.as_ref().and_then(|p| p.get_value::<String, _>(n).ok()).unwrap_or_default();
            eps.push(Endpoint {
                name: get("{a45c254e-df1c-4efd-8020-67d146a850e0},2"),
                adapter: get("{b3f8fa53-0004-438e-9003-51a46e139bfc},6"),
                installed: ids.iter().any(|i| i.eq_ignore_ascii_case(&id)),
                id,
            });
        }
    }
    eps.sort_by(|a, b| b.installed.cmp(&a.installed).then(a.adapter.cmp(&b.adapter)));
    (registered, eps)
}
#[cfg(not(windows))]
fn system_info() -> (bool, Vec<Endpoint>) { (false, Vec::new()) }

#[tauri::command]
fn get_state() -> AppState {
    let (registered, endpoints) = system_info();
    AppState {
        registered,
        endpoints,
        data_dir: data_dir().display().to_string(),
        payload_ok: payload_dir().join("HearMeOutApo.dll").exists() && payload_dir().join("install.ps1").exists(),
        version: env!("CARGO_PKG_VERSION").to_string(),
    }
}

// ------------------------------------------------------------------ inštalácia (s admin právami)
#[derive(Serialize)]
struct RunResult { ok: bool, code: i32, log: String }

fn log_tail() -> String {
    let t = std::fs::read_to_string(data_dir().join("last-run.log")).unwrap_or_default();
    let lines: Vec<&str> = t.lines().filter(|l| !l.trim().is_empty() && !l.starts_with("**")).collect();
    lines[lines.len().saturating_sub(25)..].join("\n")
}

#[cfg(windows)]
fn wide(s: &str) -> Vec<u16> { s.encode_utf16().chain(std::iter::once(0)).collect() }

#[cfg(windows)]
fn run_elevated(script: &str, args: &str) -> Result<i32, String> {
    use windows_sys::Win32::Foundation::{CloseHandle, GetLastError};
    use windows_sys::Win32::System::Threading::{GetExitCodeProcess, WaitForSingleObject, INFINITE};
    use windows_sys::Win32::UI::Shell::{ShellExecuteExW, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW};
    use windows_sys::Win32::UI::WindowsAndMessaging::SW_HIDE;

    let path = payload_dir().join(script);
    if !path.exists() { return Err(format!("Chýba {}", path.display())); }
    let params = format!("-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"{}\" {}", path.display(), args);
    let verb = wide("runas");
    let file = wide("powershell.exe");
    let par = wide(&params);
    let dir = wide(&payload_dir().display().to_string());
    unsafe {
        let mut info: SHELLEXECUTEINFOW = std::mem::zeroed();
        info.cbSize = std::mem::size_of::<SHELLEXECUTEINFOW>() as u32;
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = verb.as_ptr();
        info.lpFile = file.as_ptr();
        info.lpParameters = par.as_ptr();
        info.lpDirectory = dir.as_ptr();
        info.nShow = SW_HIDE;
        if ShellExecuteExW(&mut info) == 0 {
            let e = GetLastError();
            return Err(if e == 1223 { "Zrušené – bez admin práv to nejde.".into() } else { format!("Nepodarilo sa spustiť (chyba {})", e) });
        }
        if info.hProcess.is_null() { return Ok(0); }
        WaitForSingleObject(info.hProcess, INFINITE);
        let mut code: u32 = 1;
        GetExitCodeProcess(info.hProcess, &mut code);
        CloseHandle(info.hProcess);
        Ok(code as i32)
    }
}
#[cfg(not(windows))]
fn run_elevated(_: &str, _: &str) -> Result<i32, String> { Err("Len pre Windows".into()) }

fn finish(r: Result<i32, String>) -> Result<RunResult, String> {
    let code = r?;
    Ok(RunResult { ok: code == 0, code, log: log_tail() })
}

#[tauri::command]
async fn install_on(endpoint_id: String) -> Result<RunResult, String> {
    if !endpoint_id.chars().all(|c| c.is_ascii_hexdigit() || "{}-.".contains(c)) {
        return Err("Neplatné ID zariadenia.".into());
    }
    tauri::async_runtime::spawn_blocking(move || finish(run_elevated("install.ps1", &format!("-EndpointId \"{}\" -Quiet", endpoint_id))))
        .await
        .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn uninstall_all() -> Result<RunResult, String> {
    tauri::async_runtime::spawn_blocking(|| finish(run_elevated("uninstall.ps1", "-Quiet")))
        .await
        .map_err(|e| e.to_string())?
}

#[tauri::command]
fn open_data_dir() {
    let _ = std::process::Command::new("explorer.exe").arg(data_dir()).spawn();
}

// ------------------------------------------------------------------ živý meter
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct MeterShared {
    magic: u32, version: u32, seq: u32, locked: u32, sample_rate: u32, channels: u32,
    in_db: f32, out_db: f32, gate_db: f32, comp_gr_db: f32, latency_ms: f32,
    pid: u32, blocks: u64,
    deess_db: f32, engine_state: f32,
}

#[derive(Serialize, Clone)]
struct MeterEvent {
    connected: bool,   // zdieľaná pamäť existuje (APO sa v tomto behu Windows aspoň raz načítalo)
    active: bool,      // zvuk práve tečie cez efekt
    in_db: f32, out_db: f32, gate_db: f32, comp_gr_db: f32, deess_db: f32,
    sample_rate: u32, latency_ms: f32,
    monitoring: bool,
    engine_state: u32,
}

#[cfg(windows)]
struct Mapping(*const MeterShared, bool);   // bool = verzia >= 2
#[cfg(windows)]
unsafe impl Send for Mapping {}

#[cfg(windows)]
fn open_meter() -> Option<Mapping> {
    use windows_sys::Win32::Foundation::CloseHandle;
    use windows_sys::Win32::System::Memory::{MapViewOfFile, OpenFileMappingW, FILE_MAP_READ};
    unsafe {
        let name = wide("Global\\HearMeOutMeter");
        let h = OpenFileMappingW(FILE_MAP_READ, 0, name.as_ptr());
        if h.is_null() { return None; }
        let v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);   // celé (staršie APO má menšiu štruktúru)
        CloseHandle(h);   // view drží mapovanie nažive
        if v.Value.is_null() { return None; }
        let p = v.Value as *const MeterShared;
        if std::ptr::read_volatile(&(*p).magic) != 0x314F_4D48 { return None; }
        let v2 = std::ptr::read_volatile(&(*p).version) >= 2;
        Some(Mapping(p, v2))
    }
}

fn meter_loop(app: tauri::AppHandle, active: Arc<AtomicBool>, mon: Arc<AtomicBool>) {
    #[cfg(windows)]
    {
        let mut map: Option<Mapping> = None;
        let mut last_try = Instant::now() - Duration::from_secs(10);
        let mut last_seq = 0u32;
        let mut last_change = Instant::now() - Duration::from_secs(10);
        loop {
            if !active.load(Ordering::Relaxed) { std::thread::sleep(Duration::from_millis(250)); continue; }
            if map.is_none() && last_try.elapsed() > Duration::from_secs(1) {
                map = open_meter();
                last_try = Instant::now();
            }
            let ev = match &map {
                Some(m) => {
                    let mut s = MeterShared::default();
                    unsafe {
                        // staršie APO (v1) má len 56 bajtov – čítaj iba to, čo existuje
                        let n = if m.1 { std::mem::size_of::<MeterShared>() } else { 56 };
                        std::ptr::copy_nonoverlapping(m.0 as *const u8, &mut s as *mut MeterShared as *mut u8, n);
                    }
                    if s.seq != last_seq { last_seq = s.seq; last_change = Instant::now(); }
                    let flowing = s.locked == 1 && last_change.elapsed() < Duration::from_millis(400);
                    MeterEvent {
                        connected: true, active: flowing,
                        in_db: if flowing { s.in_db } else { -120.0 },
                        out_db: if flowing { s.out_db } else { -120.0 },
                        gate_db: if flowing { s.gate_db } else { 0.0 },
                        comp_gr_db: if flowing { s.comp_gr_db } else { 0.0 },
                        deess_db: if flowing { s.deess_db } else { 0.0 },
                        sample_rate: s.sample_rate, latency_ms: s.latency_ms,
                        monitoring: mon.load(Ordering::Relaxed),
                        engine_state: if m.1 { s.engine_state as u32 } else { 0 },
                    }
                }
                None => MeterEvent { connected: false, active: false, in_db: -120.0, out_db: -120.0, gate_db: 0.0, comp_gr_db: 0.0, deess_db: 0.0, sample_rate: 0, latency_ms: 0.0, monitoring: mon.load(Ordering::Relaxed), engine_state: 0 },
            };
            let _ = app.emit("meter", ev);
            std::thread::sleep(Duration::from_millis(33));
        }
    }
    #[cfg(not(windows))]
    { let _ = (app, active, mon); }
}

struct MeterSwitch(Arc<AtomicBool>);

// ------------------------------------------------------------------ odposluch
#[cfg(windows)]
#[tauri::command]
async fn monitor_start(endpoint_id: String, mon: tauri::State<'_, monitor::Monitor>) -> Result<String, String> {
    let (_, eps) = system_info();
    let ep = eps.into_iter().find(|e| e.id.eq_ignore_ascii_case(&endpoint_id)).ok_or("Mikrofón nie je pripojený.")?;
    mon.start(vec![ep.name, ep.adapter])
}
#[cfg(windows)]
#[tauri::command]
fn monitor_stop(mon: tauri::State<monitor::Monitor>) { mon.stop(); }
#[cfg(windows)]
#[tauri::command]
fn monitor_volume(v: f32, mon: tauri::State<monitor::Monitor>) { mon.set_volume(v); }

#[cfg(not(windows))]
#[tauri::command]
async fn monitor_start(endpoint_id: String) -> Result<String, String> { let _ = endpoint_id; Err("Len pre Windows".into()) }
#[cfg(not(windows))]
#[tauri::command]
fn monitor_stop() {}
#[cfg(not(windows))]
#[tauri::command]
fn monitor_volume(v: f32) { let _ = v; }

#[tauri::command]
fn set_meter_active(on: bool, sw: tauri::State<MeterSwitch>) { sw.0.store(on, Ordering::Relaxed); }

// ------------------------------------------------------------------ štart
fn main() {
    let active = Arc::new(AtomicBool::new(true));
    #[cfg(windows)]
    let mon = monitor::Monitor::new();
    #[cfg(windows)]
    let mon_flag = mon.running.clone();
    #[cfg(not(windows))]
    let mon_flag = Arc::new(AtomicBool::new(false));
    let builder = tauri::Builder::default().manage(MeterSwitch(active.clone()));
    #[cfg(windows)]
    let builder = builder.manage(mon);
    builder
        .setup(move |app| {
            let handle = app.handle().clone();
            let a = active.clone();
            let m = mon_flag.clone();
            std::thread::spawn(move || meter_loop(handle, a, m));
            if let Some(w) = app.get_webview_window("main") { let _ = w.set_title("HearMeOut"); }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            load_config, save_config, list_presets, save_preset, delete_preset,
            get_state, install_on, uninstall_all, open_data_dir, set_meter_active,
            monitor_start, monitor_stop, monitor_volume,
            host::vst_scan, host::vst_chain_get, host::vst_chain_set, host::host_status,
            host::host_start, host::host_stop, host::host_open_editor, host::host_autostart,
            update::update_check, update::update_install
        ])
        .run(tauri::generate_context!())
        .expect("HearMeOut sa nepodarilo spustiť");
}
