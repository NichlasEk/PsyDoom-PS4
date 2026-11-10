Local SDL joystick override copied from the pinned dependency source.
Original SHA256: e0876cffce606ef08b4f4563565de1a6ca258aef6e75ec624299a9d77b4a3ce6
SDL license retained in sdl_joystick.c. Sibling source/archive unchanged.
Changes: six contiguous axis indices (right stick 3/4, R2 5),
8-bit to signed-16-bit conversion reaches both endpoints.
Input.cpp mapping matches this layout; fabsf avoids SDK integer abs truncation.
