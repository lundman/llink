//
//  MyTableController.m
//  llink
//
//  Created by lundman on 09/09/11.
//  Copyright 2023 __MyCompanyName__. All rights reserved.
//

#import "MyTableController.h"

@implementation MyTableController
@synthesize nsMutaryDataObj; 
@synthesize idTableView; 

- (void)awakeFromNib { 
    self.nsMutaryDataObj = [[NSMutableArray alloc]init]; 
#if 0
    int i; 
    for (i = 0; i < 10; i ++) { 
        NSString * zStr1 = [[NSString alloc]initWithFormat:@"%d",(i+1)*10]; 
        NSString * zStr2 = [[NSString alloc]initWithFormat:@"%d",(i+1)*100]; 
       
        MyDataObject * zDataObject = [[MyDataObject alloc]initWithString1:zStr1 andString2:zStr2]; 
        [self.nsMutaryDataObj addObject:zDataObject];
    } // end for 
#endif
    
    [idTableView reloadData]; 
    
} // end awakeFromNib 

- (IBAction)addAtSelectedRow:(id)pId {
    if ([idTableView selectedRow] > -1) { 
        NSString * zStr1 = @"Text Cell 1"; 
        NSString * zStr2 = @"Text Cell 2"; 
       MyDataObject * zDataObject = [[MyDataObject alloc]initWithString1:zStr1 andString2:zStr2]; 
        [self.nsMutaryDataObj insertObject:zDataObject atIndex:[idTableView selectedRow]]; 
        [idTableView reloadData]; 
    } // end if 
} // end deleteSelectedRow 


- (void)addRow:(MyDataObject *)pDataObj { 
    [self.nsMutaryDataObj addObject:pDataObj]; 
    [idTableView reloadData];
} // end addRow 

- (unsigned long)numberOfRowsInTableView:(NSTableView *)pTableViewObj { 
    return [self.nsMutaryDataObj count]; 
} // end numberOfRowsInTableView 

- (id) tableView:(NSTableView *)pTableViewObj objectValueForTableColumn:(NSTableColumn *)pTableColumn row:(int)pRowIndex { 
    MyDataObject * zDataObject = (MyDataObject *) [self.nsMutaryDataObj objectAtIndex:pRowIndex]; 
    if (! zDataObject) { 
        NSLog(@"tableView: objectAtIndex:%d = NULL",pRowIndex); 
        return NULL; 
    } // end if 
    //NSLog(@"pTableColumn identifier = %@",[pTableColumn identifier]); 
    if ([[pTableColumn identifier] isEqualToString:@"Col_ID1"]) { 
        return [zDataObject nsStrName1]; 
    } 
    if ([[pTableColumn identifier] isEqualToString:@"Col_ID2"]) { 
        return [zDataObject nsStrName2]; 
    } 
    NSLog(@"***ERROR** dropped through pTableColumn identifiers"); 
    return NULL; 
} // end tableView:objectValueForTableColumn:row: 

- (void)tableView:(NSTableView *)pTableViewObj setObjectValue:(id)pObject forTableColumn:(NSTableColumn *)pTableColumn row:(int)pRowIndex { 
    MyDataObject * zDataObject = (MyDataObject *) [self.nsMutaryDataObj objectAtIndex:pRowIndex]; 
    if ([[pTableColumn identifier] isEqualToString:@"Col_ID1"]) { 
        [zDataObject setNsStrName1:(NSString *)pObject]; 
    } 
    if ([[pTableColumn identifier] isEqualToString:@"Col_ID2"]) { 
        [zDataObject setNsStrName2:(NSString *)pObject]; 
    } 

} // end tableView:setObjectValue:forTableColumn:row: 


- (IBAction)deleteSelectedRow:(id)pId { 
    if ([idTableView selectedRow] > -1) { 
        [self.nsMutaryDataObj 
         removeObjectAtIndex:[idTableView selectedRow]]; 
        [idTableView reloadData]; 
    } // end if 
} // end deleteSelectedRow 


- (IBAction)addPathAtRow:(id)sender {
       
    // Create the File Open Dialog class.
    NSOpenPanel* openDlg = [NSOpenPanel openPanel];
    
    // Enable the selection of files in the dialog.
    [openDlg setCanChooseFiles:NO];
    
    // Enable the selection of directories in the dialog.
    [openDlg setCanChooseDirectories:YES];
    
    [openDlg setAllowsMultipleSelection:YES];
    
    // Display the dialog.  If the OK button was pressed,
    // process the files.
    if ( [openDlg runModal] == NSModalResponseOK )
    {
        // Get an array containing the full filenames of all
        // files and directories selected.
        NSArray* files = [openDlg URLs];
        int i;
        
        // Loop through all the files and process them.
        for( i = 0; i < [files count]; i++ )
        {
            NSString* fileName = [files objectAtIndex:i];
            
            // Do something with the filename.
            NSLog(@"Got Directory %@", fileName);
            
            MyDataObject *zDataObject = [[MyDataObject alloc]initWithString1:fileName andString2:@""];
            
            [self.nsMutaryDataObj addObject:zDataObject];
        }
        
        [idTableView reloadData];
 
    }
    
}
@end


