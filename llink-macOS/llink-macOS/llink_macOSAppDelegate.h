//
//  llink_macOSAppDelegate.h
//  llink-macOS
//
//  Created by lundman on 08/04/11.
//  Copyright 2023 __MyCompanyName__. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#include "MyTableController.h"



@interface llink_macOSAppDelegate : NSObject <NSApplicationDelegate> {
    NSWindow *window;
    MyTableController *myTableController;
    //NSMutableArray *llink_conf_lines;
    NSArray *llink_conf_lines;
    IBOutlet NSTextField *UIPort;
    IBOutlet NSTextField *UIName;
    IBOutlet NSTextField *UIPin;
}

@property (assign) IBOutlet NSWindow *window;

- (IBAction)StartService:(id)sender;
- (IBAction)StopService:(id)sender;
- (IBAction)RestartService:(id)sender;
- (IBAction)saveConf:(id)sender;
- (int)CheckService;
- (int)addProcess;
- (int)delProcess;
- (int)parseConf;
@end
