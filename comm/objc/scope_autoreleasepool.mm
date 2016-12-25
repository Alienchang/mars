//
//  scope_autoreleasepool.cpp
//  MicroMessenger
//
//  Created by yerungui on 12-11-30.
//  Copyright (c) 2012年 Tencent. All rights reserved.
//

#include "comm/objc/scope_autoreleasepool.h"
#import <Foundation/Foundation.h>

Scope_AutoReleasePool::Scope_AutoReleasePool()
: m_pool([[NSAutoreleasePool alloc] init])
{
}

Scope_AutoReleasePool::~Scope_AutoReleasePool()
{
    [m_pool drain];
}

void comm_export_symbols_3(){}
