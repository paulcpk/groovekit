// GrooveKit desktop shell: one window with the UI, and the engine next to it.
//
// The engine is found the way every client finds it (engine.json in the app data folder, which a
// serving engine writes): if one already serves the project (say, started by the MCP server for
// Claude), the app joins it, and both see each other's changes. Otherwise the app starts its own,
// bundled as a sidecar, which quits when the app does.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::Deserialize;
use std::net::TcpListener;
use std::path::{Path, PathBuf};
use std::process::{Child, Command};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

#[derive(Deserialize, Clone)]
struct EngineInfo {
    url: String,
    pid: i32,
    project: String,
}

#[derive(Default)]
struct Engine {
    child: Mutex<Option<Child>>,
}

fn home() -> PathBuf {
    PathBuf::from(std::env::var("HOME").unwrap_or_default())
}

fn project_folder() -> PathBuf {
    std::env::var("GROOVEKIT_PROJECT")
        .map(PathBuf::from)
        .unwrap_or_else(|_| home().join("Music").join("GrooveKit").join("Scratch"))
}

fn same_path(a: &Path, b: &Path) -> bool {
    match (a.canonicalize(), b.canonicalize()) {
        (Ok(a), Ok(b)) => a == b,
        _ => a == b,
    }
}

/// The engine serving right now, if its process is alive.
fn running_engine() -> Option<EngineInfo> {
    let file = home().join("Library/Application Support/GrooveKit/engine.json");
    let info: EngineInfo = serde_json::from_str(&std::fs::read_to_string(file).ok()?).ok()?;
    (unsafe { libc::kill(info.pid, 0) } == 0).then_some(info)
}

/// The bundled engine next to the app's executable, or the repository's debug build in development.
fn engine_executable() -> PathBuf {
    if let Ok(path) = std::env::var("GROOVEKIT_ENGINE") {
        return PathBuf::from(path);
    }

    let bundled = std::env::current_exe().ok().and_then(|exe| exe.parent().map(|dir| dir.join("groovekit-engine")));

    match bundled {
        Some(path) if path.exists() => path,
        _ => Path::new(env!("CARGO_MANIFEST_DIR")).join("../../build/dev/engine/groovekit-engine"),
    }
}

fn free_port() -> std::io::Result<u16> {
    Ok(TcpListener::bind("127.0.0.1:0")?.local_addr()?.port())
}

/// The URL of the engine for the project, starting one if needed.
fn ensure_engine(state: &Engine) -> Result<String, String> {
    let project = project_folder();

    if let Some(info) = running_engine() {
        if same_path(Path::new(&info.project), &project) {
            return Ok(info.url);
        }
    }

    let mut child_slot = state.child.lock().map_err(|e| e.to_string())?;

    // Ours may still be starting (or have died, in which case start another).
    let alive = child_slot.as_mut().map(|c| matches!(c.try_wait(), Ok(None))).unwrap_or(false);

    if !alive {
        std::fs::create_dir_all(&project).map_err(|e| e.to_string())?;
        let port = free_port().map_err(|e| e.to_string())?;
        let child = Command::new(engine_executable())
            .arg("serve")
            .arg(&project)
            .args(["--port", &port.to_string(), "--parent-pid", &std::process::id().to_string()])
            .spawn()
            .map_err(|e| format!("Couldn't start the engine: {e}"))?;
        *child_slot = Some(child);
    }

    let pid = child_slot.as_ref().map(|c| c.id() as i32).unwrap_or(0);
    drop(child_slot);

    // A large sample library takes a few seconds to load before the engine listens.
    let started = Instant::now();

    while started.elapsed() < Duration::from_secs(90) {
        if let Some(info) = running_engine() {
            if info.pid == pid {
                return Ok(info.url);
            }
        }

        std::thread::sleep(Duration::from_millis(150));
    }

    Err("The engine didn't start".into())
}

#[tauri::command]
async fn engine_url(state: tauri::State<'_, Arc<Engine>>) -> Result<String, String> {
    // Waiting for a starting engine mustn't block the main thread.
    let engine = state.inner().clone();
    tauri::async_runtime::spawn_blocking(move || ensure_engine(&engine))
        .await
        .map_err(|e| e.to_string())?
}

fn main() {
    tauri::Builder::default()
        .manage(Arc::new(Engine::default()))
        .invoke_handler(tauri::generate_handler![engine_url])
        .build(tauri::generate_context!())
        .expect("couldn't build the app")
        .run(|app, event| {
            if let tauri::RunEvent::Exit = event {
                use tauri::Manager;
                if let Some(mut child) = app.state::<Arc<Engine>>().child.lock().ok().and_then(|mut c| c.take()) {
                    let _ = child.kill();
                }
            }
        });
}
