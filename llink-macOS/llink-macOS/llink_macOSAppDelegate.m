//
//  llink_macOSAppDelegate.m
//  llink-macOS
//
//  Created by lundman on 08/04/11.
//  Copyright 2023 __MyCompanyName__. All rights reserved.
//

#import "llink_macOSAppDelegate.h"
#include "MyDataObject.h"


#define LLINK_CONF @"~/Library/Application Support/llink/llink-2.3.1.conf";



@implementation llink_macOSAppDelegate

@synthesize window;


- (NSString *)documentsDirectory {
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    return [paths objectAtIndex:0];
}


- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    // Insert code here to initialize your application
    
    NSFileManager *fileManager = [NSFileManager defaultManager]; 
    NSString *folder = @"~/Library/Application Support/llink/"; 
    folder = [folder stringByExpandingTildeInPath]; 
    
    if ([fileManager fileExistsAtPath: folder] == NO) {

        if ([fileManager createDirectoryAtPath: folder withIntermediateDirectories: NO attributes: nil error:nil]) {
            
            NSAlert *alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle:@"Sure"];
            //[alert addButtonWithTitle:@"Cancel"];
            [alert setMessageText: @"Welcome to llink!"];
            [alert setInformativeText: @"As this might be your first time to run llink, the background service needs to be started before you can use it. Use the Menu->StartService to do so.\n\nYou can then use your Browser to connect to the llink port. The default is http://localhost:8001/ \n\nIf you decide to change llink.conf, llink needs to be restarted to reconfigure for the changes.\nUse the Menu->RestartService to do so."];
            [alert setAlertStyle: NSAlertStyleWarning];
            
            [alert runModal];
            
            [alert release];
            
        }
    }
    
    
    
    // 1) If there is no "Application Data/llink/llink.conf" we will copy it there.
    // 2) Read in this conf
    // 3) Parse it.
    
    // "Application Data"
    NSString *filename = LLINK_CONF; 
    filename = [filename stringByExpandingTildeInPath]; 
    // "Application Bundle"
    NSString *filePath = [[[NSBundle mainBundle] bundlePath] stringByAppendingString:@"/Contents/Resources/llink.conf"];

    if ([fileManager fileExistsAtPath: filename] == NO) {
        NSLog(@"No conf, so saving default");
        [fileManager copyItemAtPath:filePath toPath: filename error: nil];
    }
    
    
    // Parse the llink.conf file into lines
    // I believe the "copy" at the end makes a new NSArray which is allocated.
    NSString *contents = [NSString stringWithContentsOfFile:filename encoding:NSASCIIStringEncoding error:nil];
    llink_conf_lines = [[contents componentsSeparatedByCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"\r\n"]] copy];
    
    
    [self parseConf];
    
    
}

- (int)parseConf 
{
    NSLog(@"When parsing in %p", llink_conf_lines);

    for (NSString* line in llink_conf_lines) {
        if (line.length) {
            if ([line characterAtIndex: 0] == '#') continue;
            //NSLog(@"line: %@", line);
#if 0
            NSRange range = [line rangeOfString:@"HTTP" options:NSCaseInsensitiveSearch];
            if (range.location != NSNotFound) {
                NSLog(@"HTTP found %@", line);
                range = [line rangeOfString:@"port=" options:NSCaseInsensitiveSearch];
                if (range.location != NSNotFound) {
                    NSString *sub = [line substringFromIndex:(range.location + 5)];
                    port = [sub intValue];
                    NSLog(@"PORT found %u ; %@", port, line);
                } // NotFound
            } // Not Found
#endif

            // Split up the line by "|" separator, so index 0 is the command.
            NSArray *listItems = [line componentsSeparatedByString:@"|"];

            if ([listItems count] <= 0) continue;

            if ([[listItems objectAtIndex: 0] hasPrefix: @"ROOT"]) {
                NSString *path = NULL;
                NSString *subdir = NULL;
                
                NSLog(@"parseConf: hasPrefix ROOT: %@", line);

                // Find the element that has "path=" 
                for (NSString *part in listItems) {
                    if ([part hasPrefix: @"path="]) {
                        path = [part substringFromIndex: 5];
                    }
                    if ([part hasPrefix: @"subdir="]) {
                        subdir = [part substringFromIndex: 7];
                    }
                } // for all parts

                // If we read in a path, add it to view.
                if (path.length) {
                    MyDataObject *zDataObject = [[MyDataObject alloc]initWithString1:path andString2:subdir];
                    [myTableController addRow: zDataObject];

                } // if path
            
            } else if ([[listItems objectAtIndex: 0] hasPrefix: @"HTTP"]) { // if ROOT
                // name= port= pin=
                // Find the element that has "path=" 
                for (NSString *part in listItems) {
                    if ([part hasPrefix: @"port="]) {
                        NSLog(@"port=");
                        [UIPort setStringValue:[part substringFromIndex: 5]];
                    }
                    if ([part hasPrefix: @"name="]) {
                        NSLog(@"name=");
                        [UIName setStringValue:[part substringFromIndex: 5]];
                    }
                    if ([part hasPrefix: @"pin="]) {
                        NSLog(@"pin=");
                        [UIPin setStringValue:[part substringFromIndex: 4]];
                    }
                } // for all parts
            } // If ROOT, PORT
     
        } // if length
    }
    return 0;
}



- (IBAction)saveConf:(id)sender {
    int firstroot = 1;
    NSString *filename = LLINK_CONF; 
    filename = [filename stringByExpandingTildeInPath]; 
    
    [[NSFileManager defaultManager] createFileAtPath:filename contents:nil attributes:nil];
    NSFileHandle *fh = [NSFileHandle fileHandleForWritingAtPath:filename];
    
    if (fh != nil) {
        
        for (NSString* line in llink_conf_lines) {
            if (line.length) {
                
                // If line starts with ROOT, save our ROOTs instead. Skip any following ROOTs.
                // Split up the line by "|" separator, so index 0 is the command.
                NSArray *listItems = [line componentsSeparatedByString:@"|"];
                
                if (([listItems count] > 1) && ([[listItems objectAtIndex: 0] hasPrefix: @"ROOT"])) {
                    
                    // Line starts with ROOT
                    if (firstroot) {
                        firstroot = 0;
                        for (MyDataObject *root in myTableController.nsMutaryDataObj) {
                            if (!root.nsStrName2.length)
                                [fh writeData:[[NSString stringWithFormat:@"ROOT|path=%@\r\n", root.nsStrName1] dataUsingEncoding:NSUTF8StringEncoding]];
                            else
                                [fh writeData:[[NSString stringWithFormat:@"ROOT|path=%@|subdir=%@\r\n", root.nsStrName1, root.nsStrName2] dataUsingEncoding:NSUTF8StringEncoding]];
                        }
                        
                    }


                } else if (([listItems count] > 1) && ([[listItems objectAtIndex: 0] hasPrefix: @"HTTP"])) {

                    // HTTP|name=|port=|pin=
                    [fh writeData:[[NSString stringWithFormat:@"HTTP|port=%@|name=%@|pin=%@\r\n", 
                                    [UIPort stringValue], [UIName stringValue], [UIPin stringValue]] 
                                   dataUsingEncoding:NSUTF8StringEncoding]];
                    
                } else {
                    
                    // Not ROOT, write to conf
                    [fh writeData:[[NSString stringWithFormat:@"%@\r\n", line] dataUsingEncoding:NSUTF8StringEncoding]];
                    
                }
            }
        }
        
        [fh closeFile];
    }
    NSLog(@"Saved llink.conf");
    
}





- (IBAction)edit:(id)sender {
    NSString *filename = LLINK_CONF; 
    filename = [filename stringByExpandingTildeInPath]; 

    NSLog(@"Menu: Edit llink.conf %@", filename);
    //[[NSWorkspace sharedWorkspace] launchApplication: @"Textedit.app"];

    [[NSWorkspace sharedWorkspace]openFile: filename withApplication: @"Textedit.app"];
}

// 0 means it defined and running
// 1 means it is defined but not running
// 2 means it is not defined.
- (int)CheckService {
    NSTask *task = [NSTask new];
    [task setLaunchPath:@"/bin/launchctl"];
    [task setArguments:[NSArray arrayWithObjects: @"list",  @"net.lundman.llink", nil]];
    [task setStandardOutput:[NSPipe pipe]];
    [task setStandardError:[task standardOutput]];
    [task launch];
    NSData* output = [[[task standardOutput] fileHandleForReading] readDataToEndOfFile];
    NSString* out_string = [[[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding] autorelease];
    //NSLog(@"Process: %@", out_string);
    if ([out_string rangeOfString:@"PID"].location != NSNotFound) {
        NSLog(@"launchctl has PID, so service is running.");
        [task waitUntilExit];
        return 0;
    }
    if ([out_string rangeOfString:@"Label"].location != NSNotFound) {
        NSLog(@"launchctl has label, so service is defined.");
        [task waitUntilExit];
        return 1;
    }
    NSLog(@"launchctl has no label, so service not defined.");
    [task waitUntilExit];
    return 2;
}

- (int)addProcess {

    NSString *pwd_path = [[[NSBundle mainBundle] bundlePath] stringByAppendingString:@"/Contents/Resources/"];
    NSString *llink_path = [pwd_path stringByAppendingString:@"llink"];

    NSString *filename = LLINK_CONF; 
    filename = [filename stringByExpandingTildeInPath]; 

    NSString *plistname = @"~/Library/Application Support/llink/llink.plist"; 
    plistname = [plistname stringByExpandingTildeInPath]; 
    NSString *plist = [[NSString alloc] initWithFormat: 
                       @"<?xml version=”1.0″ encoding=”UTF-8″?>\r\n"
                        "<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\r\n"
                        "<plist version=\"1.0\">\r\n"
                        "<dict>\r\n"
                        "<key>Label</key>\r\n"
                        "<string>net.lundman.llink</string>\r\n"
                        "<key>ProgramArguments</key>\r\n"
                        "<array>\r\n"
                        "<string>%@</string>\r\n"
                        "<string>-dw</string>\r\n"
                        "<string>%@</string>\r\n"
                        "<string>-f</string>\r\n"
                        "<string>%@</string>\r\n"
                        "</array>\r\n"
                        "<key>KeepAlive</key>\r\n"
                        "<true/>\r\n"
                        "</dict>\r\n"
                        "</plist>\r\n",
                       llink_path,
                       pwd_path,
                       filename];

    
    [plist writeToFile:plistname atomically:NO encoding:NSUTF8StringEncoding error:nil];
    
    NSTask *task = [NSTask new];
    [task setLaunchPath:@"/bin/launchctl"];
    //[task setArguments:[NSArray arrayWithObjects: @"submit",  @"-l",  @"net.lundman.llink", @"-p", llink_path, 
      //                  @"--", @"llink",  @"-dw", pwd_path, @"-f", filename, nil]];
    [task setArguments:[NSArray arrayWithObjects: @"load",  plistname, nil]];
                                                                         
    [task setStandardOutput:[NSPipe pipe]];
    [task setStandardError:[task standardOutput]];
    [task launch];
    NSData* output = [[[task standardOutput] fileHandleForReading] readDataToEndOfFile];
    NSString* out_string = [[[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding] autorelease];
    NSLog(@"addProcess: %@", out_string);
    [task waitUntilExit];
    return 0;
}

- (int)delProcess {
    NSTask *task = [NSTask new];
    [task setLaunchPath:@"/bin/launchctl"];
    [task setArguments:[NSArray arrayWithObjects: @"remove",  @"net.lundman.llink", nil]];
    [task setStandardOutput:[NSPipe pipe]];
    [task setStandardError:[task standardOutput]];
    [task launch];
    NSData* output = [[[task standardOutput] fileHandleForReading] readDataToEndOfFile];
    NSString* out_string = [[[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding] autorelease];
    NSLog(@"delProcess: %@", out_string);
    [task waitUntilExit];
    return 0;
}




- (IBAction)StartService:(id)sender {
    int check;
    
    check = [self CheckService];
    NSLog(@"CheckService: %d", check);
    switch(check) {
        case 2:
        {
            NSAlert *alert = [[NSAlert alloc] init];
            long value;
            [alert addButtonWithTitle:@"OK"];
            [alert addButtonWithTitle:@"Cancel"];
            [alert setMessageText: @"Starting background process"];
            //[alert setShowsSuppressionButton: true];
            [alert setInformativeText: @"llink's StartService runs a process in the background, even after this Application quits.\n\nTo stop the service, run this Application again, and pick StopService in the menu. This completely uninstalls the llink service\n\nOnce the llink service is running, you can test it by connecting to it with a Browser, or directly with your MediaPlayer or UPNP client."];
            [alert setAlertStyle: NSAlertStyleWarning];
            
            value = [alert runModal];
            
            //[alert release];
            
            if (value != NSAlertFirstButtonReturn) break;
            
            [self saveConf: nil];
            
            [self addProcess];


            check = [self CheckService];
            NSLog(@"CheckService: %d", check);
            [alert init];
            if (check == 0)
                [alert setMessageText: @"Service has started."];
            else
                [alert setMessageText: @"Service failed to start."];
            //[alert setShowsSuppressionButton: false];
            [alert runModal];
            [alert release];
        }
            
            break;
        default:
        {
            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText: @"Service is already running"];
            [alert runModal];
            [alert release];
        }
            break;
    }
}

- (IBAction)StopService:(id)sender {
    [self delProcess];
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText: @"Service has stopped"];
    [alert runModal];
    [alert release];
 }


// Actually, this is open in browser!
- (IBAction)RestartService:(id)sender {
    int check;

    check = [self CheckService];
    if (check != 0) {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText: @"Service is not running, please use menu File->StartService."];
        [alert runModal];
        [alert release];
        return;
    }

#if 0
    unsigned int port = 8001;
    // Read llink.conf and attempt to find the PORT used.
    // HTTP|port=8001|pin=1234|name=llink
    NSString *filePath = [[[NSBundle mainBundle] bundlePath] stringByAppendingString:@"/Contents/Resources/llink.conf"];
    NSString *contents = [NSString stringWithContentsOfFile:filePath encoding:NSASCIIStringEncoding error:nil];
    NSArray *lines = [contents componentsSeparatedByCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"\r\n"]];
    for (NSString* line in lines) {
        if (line.length) {
            if ([line characterAtIndex: 0] == '#') continue;
                //NSLog(@"line: %@", line);
            NSRange range = [line rangeOfString:@"HTTP" options:NSCaseInsensitiveSearch];
            if (range.location != NSNotFound) {
                NSLog(@"HTTP found %@", line);
                range = [line rangeOfString:@"port=" options:NSCaseInsensitiveSearch];
                if (range.location != NSNotFound) {
                    NSString *sub = [line substringFromIndex:(range.location + 5)];
                    port = [sub intValue];
                    NSLog(@"PORT found %u ; %@", port, line);
                }
            }
        }
    }
#endif 
    
    NSString *surl = [[NSString alloc] initWithFormat: @"http://localhost:%@/", [UIPort stringValue]];
    NSLog(@"Launching Browser for : %@", surl);
    NSURL *url = [NSURL URLWithString:surl];
    [[NSWorkspace sharedWorkspace]openURL: url];

}




@end
