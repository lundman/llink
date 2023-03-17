//
//  MyTableController.h
//  llink
//
//  Created by lundman on 09/09/11.
//  Copyright 2023 __MyCompanyName__. All rights reserved.
//

#import <Cocoa/Cocoa.h> 
#import "MyDataObject.h" 

//@interface MyTableController : NSControl { 
@interface MyTableController : NSObject { 
    NSMutableArray * nsMutaryDataObj; 
    IBOutlet NSTableView * idTableView; 
} 

@property (assign) NSMutableArray * nsMutaryDataObj; 
@property (assign) NSTableView * idTableView; 

- (IBAction)addAtSelectedRow:(id)pId; 

- (IBAction)deleteSelectedRow:(id)pId; 

- (void)addRow:(MyDataObject *)pDataObj; 

- (unsigned long)numberOfRowsInTableView:(NSTableView *)pTableViewObj; 

- (id) tableView:(NSTableView *)pTableViewObj objectValueForTableColumn:(NSTableColumn *)pTableColumn row:(int)pRowIndex; 

- (void)tableView:(NSTableView *)pTableViewObj setObjectValue:(id)pObject forTableColumn:(NSTableColumn *)pTableColumn row:(int)pRowIndex; 

- (IBAction)addPathAtRow:(id)sender;

@end
