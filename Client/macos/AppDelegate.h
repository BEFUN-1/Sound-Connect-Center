//
//  AppDelegate.h
//  SonyHeadphonesClient
//
//  Application delegate for Sound Connect Center.
//

#import <Cocoa/Cocoa.h>
#import <stdio.h>
#import <IOBluetoothUI/IOBluetoothUI.h>
#import "BluetoothWrapper.h"
#import "Headphones.h"
#import "MacOSBluetoothConnector.h"

@interface AppDelegate : NSObject <NSApplicationDelegate>
    @property (weak) NSWindow* window;

@end
