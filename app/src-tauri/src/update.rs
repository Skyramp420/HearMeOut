// Aktualizácie z GitHub Releases (Skyramp420/HearMeOut): zistí najnovšiu verziu, stiahne inštalátor a spustí ho.
use serde::Serialize;

const REPO: &str = "Skyramp420/HearMeOut";

#[derive(Serialize, Clone)]
pub struct UpdateInfo {
    pub current: String,
    pub latest: String,
    pub available: bool,
    pub notes: String,
    pub url: String,      // inštalátor (.exe)
    pub page: String,     // stránka releasu
}

fn parse_ver(v: &str) -> Vec<u64> {
    v.trim_start_matches('v').split(|c: char| !c.is_ascii_digit()).filter(|s| !s.is_empty()).map(|s| s.parse().unwrap_or(0)).collect()
}
fn newer(a: &str, b: &str) -> bool {
    let (x, y) = (parse_ver(a), parse_ver(b));
    for i in 0..x.len().max(y.len()) {
        let (p, q) = (*x.get(i).unwrap_or(&0), *y.get(i).unwrap_or(&0));
        if p != q { return p > q; }
    }
    false
}

fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new().timeout(std::time::Duration::from_secs(20)).user_agent("HearMeOut-updater").build()
}

#[tauri::command]
pub async fn update_check() -> Result<UpdateInfo, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let current = env!("CARGO_PKG_VERSION").to_string();
        let resp = agent()
            .get(&format!("https://api.github.com/repos/{REPO}/releases/latest"))
            .set("Accept", "application/vnd.github+json")
            .call()
            .map_err(|e| format!("Nepodarilo sa overiť aktualizácie: {e}"))?;
        let j: serde_json::Value = resp.into_json().map_err(|e| e.to_string())?;
        let latest = j["tag_name"].as_str().unwrap_or("").trim_start_matches('v').to_string();
        let url = j["assets"].as_array().into_iter().flatten()
            .filter_map(|a| a["browser_download_url"].as_str())
            .find(|u| u.to_lowercase().ends_with("-setup.exe"))
            .unwrap_or("").to_string();
        Ok(UpdateInfo {
            available: !latest.is_empty() && !url.is_empty() && newer(&latest, &current),
            notes: j["body"].as_str().unwrap_or("").to_string(),
            page: j["html_url"].as_str().unwrap_or("").to_string(),
            current, latest, url,
        })
    }).await.map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn update_install(url: String, app: tauri::AppHandle) -> Result<(), String> {
    if !url.starts_with(&format!("https://github.com/{REPO}/releases/download/")) {
        return Err("Neplatná adresa aktualizácie.".into());
    }
    let path = tauri::async_runtime::spawn_blocking(move || -> Result<std::path::PathBuf, String> {
        let resp = agent().get(&url).call().map_err(|e| format!("Sťahovanie zlyhalo: {e}"))?;
        let dir = std::env::temp_dir().join("HearMeOut-update");
        let _ = std::fs::create_dir_all(&dir);
        let file = dir.join(url.rsplit('/').next().unwrap_or("HearMeOut-setup.exe"));
        let mut out = std::fs::File::create(&file).map_err(|e| e.to_string())?;
        std::io::copy(&mut resp.into_reader(), &mut out).map_err(|e| format!("Sťahovanie zlyhalo: {e}"))?;
        Ok(file)
    }).await.map_err(|e| e.to_string())??;
    // host drží svoj .exe otvorený – ukonči ho slušne (uloží stavy pluginov), inštalátor ho potom spustí znova
    crate::host::host_stop();
    for _ in 0..30 { if !crate::host::host_running() { break; } std::thread::sleep(std::time::Duration::from_millis(100)); }
    // /P = inštalácia bez otázok, len s priebehom
    std::process::Command::new(&path).arg("/P").spawn().map_err(|e| format!("Inštalátor sa nespustil: {e}"))?;
    app.exit(0);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::newer;
    #[test]
    fn versions() {
        assert!(newer("0.5.0", "0.4.0"));
        assert!(newer("v1.0", "0.9.9"));
        assert!(!newer("0.4.0", "0.4.0"));
        assert!(newer("0.4.10", "0.4.9"));
        assert!(!newer("0.3.9", "0.4.0"));
    }
}
