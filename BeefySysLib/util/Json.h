#pragma once

/*
  Copyright (c) 2009 Dave Gamble
 
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
 
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
 
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include "../Common.h"

NS_BF_BEGIN

/* The cJSON structure: */


class Json
{
public:
	/* cJSON Types: */
	enum Type
	{
		Type_False,
		Type_True,
		Type_NULL,
		Type_Number,
		Type_String,
		Type_Array,
		Type_Object,

		Type_IsReference = 256
	};

	Json* mNext;
	Json* mPrev;			/* next/prev allow you to walk array/object chains. Alternatively, use GetArraySize/GetArrayItem/GetObjectItem */
	Json* mChild;		/* An array or object item will have a child pointer pointing to a chain of the items in the array/object. */

	Type mType;					/* The type of the item, as above. */

	char* mValueString;			/* The item's string, if type==cJSON_String */
	int mValueInt;				/* The item's number, if type==cJSON_Number */
	double mValueDouble;			/* The item's number, if type==cJSON_Number */

	char* mName;				/* The item's name string, if this item is the child of, or is in the list of subitems of an object. */	

	const char* mSrcStart;
	const char* mSrcEnd;

	struct Hooks 
	{
		void *(*malloc_fn)(size_t sz);
		void(*free_fn)(void *ptr);
	};
	static void *(*malloc)(size_t sz);
	static void (*free)(void *ptr);

public:	
	~Json();

	/* Supply malloc, realloc and free functions to Json */
	void InitHooks(Hooks* hooks);

	/* Render a cJSON entity to text for transfer/storage. Free the char* when finished. */
	char* Print();

	/* Supply a block of JSON, and this returns a cJSON object you can interrogate. Call Delete when finished. */
	static Json* Parse(const char *value);
	
	/* Render a cJSON entity to text for transfer/storage without any formatting. Free the char* when finished. */
	char *PrintUnformatted();
	
	/* Returns the number of items in an array (or object). */
	int	GetArraySize();
	/* Retrieve item number "item" from array "array". Returns NULL if unsuccessful. */
	Json* GetArrayItem(int item);
	/* Get item "string" from object. Case insensitive. */
	Json* GetObjectItem(const char *string);

	/* For analysing failed parses. This returns a pointer to the parse error. You'll probably need to look a few chars back to make sense of it. Defined when Parse() returns 0. 0 when Parse() succeeds. */
	static const char *GetErrorPtr(void);

	/* These calls create a cJSON item of the appropriate type. */
	static Json* CreateNull(void);
	static Json* CreateTrue(void);
	static Json* CreateFalse(void);
	static Json* CreateBool(int b);
	static Json* CreateNumber(double num);
	static Json* CreateString(const char *string);
	static Json* CreateArray(void);
	static Json* CreateObject(void);

	/* These utilities create an Array of count items. */
	static Json* CreateIntArray(const int *numbers, int count);
	static Json* CreateFloatArray(const float *numbers, int count);
	static Json* CreateDoubleArray(const double *numbers, int count);
	static Json* CreateStringArray(const char **strings, int count);

	/* Append item to the specified array/object. */
	void AddItemToArray(Json* item);
	void AddItemToObject(const char *string, Json* item);
	/* Append reference to item to the specified array/object. Use this when you want to add an existing cJSON to a new cJSON, but don't want to corrupt your existing cJSON. */
	void AddItemReferenceToArray(Json* item);
	void AddItemReferenceToObject(const char *string, Json* item);

	/* Remove/Detatch items from Arrays/Objects. */
	static Json* DetachItemFromArray(Json* array, int which);
	static void DeleteItemFromArray(Json* array, int which);
	static Json* DetachItemFromObject(Json* object, const char *string);
	static void DeleteItemFromObject(Json* object, const char *string);

	/* Update array items. */
	static void ReplaceItemInArray(Json* array, int which, Json* newitem);
	static void ReplaceItemInObject(Json* object, const char *string, Json* newitem);

	/* Duplicate a cJSON item */
	static Json* Duplicate(Json* item, int recurse);
	/* Duplicate will create a new, identical cJSON item to the one you pass, in new memory that will
	need to be released. With recurse!=0, it will duplicate any children connected to the item.
	The item->next and ->prev pointers are always zero on return from Duplicate. */

	/* ParseWithOpts allows you to require (and check) that the JSON is null terminated, and to retrieve the pointer to the final byte parsed. */
	static Json* ParseWithOpts(const char *value, const char **return_parse_end, int require_null_terminated);

	/* Macros for creating things quickly. */
	void AddNullToObject(const char* name) { AddItemToObject(name, CreateNull()); }
	void AddTrueToObject(const char* name) { AddItemToObject(name, CreateTrue()); }
	void AddFalseToObject(const char* name) { AddItemToObject(name, CreateFalse()); }
	void AddBoolToObject(const char* name, bool b)	{ AddItemToObject(name, CreateBool(b)); }
	void AddNumberToObject(const char* name, int n)	{ AddItemToObject(name, CreateNumber(n)); }
	void AddNumberToObject(const char* name, float n) { AddItemToObject(name, CreateNumber(n)); }
	void AddNumberToObject(const char* name, double n) { AddItemToObject(name, CreateNumber(n)); }
	void AddStringToObject(const char* name, const char* s)	{ AddItemToObject(name, CreateString(s)); }

		/* When assigning an integer value, it needs to be propagated to valuedouble too. */
	void SetIntValue(int val) { mValueInt=val; mValueDouble=val; }

	static void Minify(char *json);
};

NS_BF_END
