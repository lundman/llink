//
//  MyDataObject.m
//  llink
//
//  Created by lundman on 09/09/11.
//  Copyright 2023 __MyCompanyName__. All rights reserved.
//

#import "MyDataObject.h"

@implementation MyDataObject
@synthesize nsStrName1; 
@synthesize nsStrName2; 

- (id)initWithString1:(NSString *)pStr1 andString2:(NSString *)pStr2 
{ 
    if (! (self = [super init])) { 
        NSLog(@"MyDataObject **** ERROR : [super init] failed ***"); 
        return self; 
    } // end if 
    self.nsStrName1 = pStr1; 
    self.nsStrName2 = pStr2; 
  
    return self; 
} // end initWithString1:andString2:andString3: 

@end
