//
//  MyDataObject.h
//  llink
//
//  Created by lundman on 09/09/11.
//  Copyright 2023 __MyCompanyName__. All rights reserved.
//



@interface MyDataObject : NSObject { 
    NSString *nsStrName1; 
    NSString *nsStrName2; 
} 
@property (copy) NSString *nsStrName1; 
@property (copy) NSString *nsStrName2; 

- (id)initWithString1:(NSString *)pStr1 andString2:(NSString *)pStr2;

@end
