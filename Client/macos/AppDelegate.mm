//
//  AppDelegate.m
//  SonyHeadphonesClient
//
//  Application delegate implementation for Sound Connect Center.
//

#import "AppDelegate.h"

@interface AppDelegate ()


@end

@implementation AppDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    _window = [[[NSApplication sharedApplication] windows] firstObject];
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)flag
{
    if (flag) {
        return NO;
    }
    else {
        [_window makeKeyAndOrderFront:self];
        return YES;
    }
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    // Insert code here to tear down your application
}

@end
