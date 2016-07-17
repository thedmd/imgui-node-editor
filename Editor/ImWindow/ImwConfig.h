
#ifndef __IM_CONFIG_H__
#define __IM_CONFIG_H__

#ifndef NULL
#define NULL 0
#endif // NULL

#ifndef ImwList
#include <list>
#define ImwList std::list
#endif // ImList

#ifndef ImwMap
#include <map>
#define ImwMap std::map
#endif // ImMap

#define ImwSafeDelete(pObj) { if (NULL != pObj) { delete pObj; pObj = NULL; } }
#define ImwSafeRelease(pObj) { if (NULL != pObj) pObj->Release(); }
#define ImwIsSafe(pObj) if (NULL != pObj) pObj

#define ImwMalloc(iSize) malloc(iSize)
#define ImwFree(pObj) free(pObj)
#define ImwSafeFree(pObj) {free(pObj); pObj = NULL;}

// Define IMW_INHERITED_BY_IMWWINDOW when you want ImwWindow inherit from one of you class
//#define IMW_INHERITED_BY_IMWWINDOW MyInheritedClass

//////////////////////////////////////////////////////////////////////////
// Include here imgui.h
//////////////////////////////////////////////////////////////////////////
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#endif // __IM_CONFIG_H__