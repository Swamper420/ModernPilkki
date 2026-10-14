// ModernPilkki — SDL window skeleton. Parity target: Propilkki 2 v2.0.7.
#[allow(dead_code)] // parity module; exercised by tests until gameplay uses it
mod msvc_rand;
#[allow(dead_code)] // parity module; exercised by tests until gameplay uses it
mod ves;

use sdl2::pixels::Color;
use sdl2::sys::{SDL_Event, SDL_EventType, SDL_KeyCode, SDL_PollEvent};

fn main() -> Result<(), String> {
    let sdl = sdl2::init()?;
    let video = sdl.video()?;
    let window = video
        .window("ModernPilkki", 640, 480)
        .build()
        .map_err(|e| e.to_string())?;
    let mut canvas = window.into_canvas().build().map_err(|e| e.to_string())?;

    'running: loop {
        // Event pump at the sys level: the sdl2 crate's safe decoder aborts on
        // event types it does not know ("trying to construct an enum from an
        // invalid value 0x207") because the system SDL2 is sdl2-compat 2.32.x,
        // which forwards unmapped SDL3 window events raw (0x207 =
        // SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED). Handle only what the skeleton
        // needs and ignore unknown types, like a C SDL2 app would.
        let mut e: SDL_Event = unsafe { std::mem::zeroed() };
        while unsafe { SDL_PollEvent(&mut e) } == 1 {
            let ty = unsafe { e.type_ };
            if ty == SDL_EventType::SDL_QUIT as u32 {
                break 'running;
            }
            if ty == SDL_EventType::SDL_KEYDOWN as u32
                && unsafe { e.key.keysym.sym } == SDL_KeyCode::SDLK_ESCAPE as i32
            {
                break 'running;
            }
        }
        canvas.set_draw_color(Color::RGB(20, 20, 30));
        canvas.clear();
        canvas.present();
        // ponytail: dummy video driver never delivers Quit; render one frame
        // and exit 0 so `SDL_VIDEODRIVER=dummy cargo run` terminates.
        if video.current_video_driver() == "dummy" {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(16));
    }
    Ok(())
}
