#import <Cocoa/Cocoa.h>

extern "C" void empower_set_dock_icon(const char* png_path) {
    if (!png_path || !*png_path) return;

    @autoreleasepool {
        NSString* path = [NSString stringWithUTF8String:png_path];
        NSImage* image = [[NSImage alloc] initWithContentsOfFile:path];
        if (image != nil) {
            [NSApp setApplicationIconImage:image];
        }
    }
}
