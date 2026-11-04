ADD_SCENE(desktop, main, Main)
ADD_SCENE(desktop, lock_menu, LockMenu)
#ifdef FURI_DEBUG
ADD_SCENE(desktop, debug, Debug)
#endif
ADD_SCENE(desktop, hw_mismatch, HwMismatch)
ADD_SCENE(desktop, fault, Fault)
ADD_SCENE(desktop, locked, Locked)
ADD_SCENE(desktop, pin_input, PinInput)
ADD_SCENE(desktop, pin_timeout, PinTimeout)
ADD_SCENE(desktop, slideshow, Slideshow)
ADD_SCENE(desktop, secure_enclave, SecureEnclave)
ADD_SCENE(desktop, clock_lock, ClockLock)
ADD_SCENE(desktop, low_ram, LowRam)