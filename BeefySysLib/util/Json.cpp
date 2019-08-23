#include "Json.h"
#include <limits.h>
#include <float.h>
#include <string.h>

#pragma warning(disable:4996)

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

/* Json */
/* JSON parser in C. */

USING_NS_BF;

static const char *ep;

const char *Json::GetErrorPtr(void) {return ep;}

static int JSON_strcasecmp(const char *s1,const char *s2)
{
	if (!s1) return (s1==s2)?0:1;if (!s2) return 1;
	for(; tolower(*s1) == tolower(*s2); ++s1, ++s2)	if(*s1 == 0)	return 0;
	return tolower(*(const unsigned char *)s1) - tolower(*(const unsigned char *)s2);
}

static char* JSON_strdup(const char* str)
{
      size_t len;
      char* copy;

      len = strlen(str) + 1;
      if (!(copy = (char*)Json::malloc(len))) return 0;
      memcpy(copy,str,len);
      return copy;
}

void *(*Json::malloc)(size_t sz) = ::malloc;
void (*Json::free)(void *ptr) = ::free;

Json::~Json()
{		
	if (!(mType&Type_IsReference) && mChild) delete mChild;
	if (!(mType&Type_IsReference) && mValueString) Json::free(mValueString);
	if (mName) Json::free(mName);	
	delete mNext;
}

void Json::InitHooks(Json::Hooks* hooks)
{
    if (!hooks) 
	{ 
		/* Reset hooks */
        Json::malloc = ::malloc;
        Json::free = ::free;
        return;
    }

	Json::malloc = (hooks->malloc_fn)?hooks->malloc_fn:malloc;
	Json::free	 = (hooks->free_fn)?hooks->free_fn:free;
}

/* Internal constructor. */
static Json* New_Item(void)
{
	/*Json* node = (Json*)Json::malloc(sizeof(Json));
	if (node) memset(node,0,sizeof(Json));	*/
	
	Json* node = new Json();	
	if (node) memset(node, 0, sizeof(Json));
	return node;
}

/* Parse the input text to generate a number, and populate the result into item. */
static const char *parse_number(Json* item,const char *num)
{
	double n=0,sign=1,scale=0;int subscale=0,signsubscale=1;

	if (*num=='-') sign=-1,num++;	/* Has sign? */
	if (*num=='0') num++;			/* is zero */
	if (*num>='1' && *num<='9')	
	{
		do	
			n=(n*10.0)+(*num++ -'0'); 
		while (*num>='0' && *num<='9');	/* Number? */
	}
	if (*num=='.' && num[1]>='0' && num[1]<='9') 
	{
		num++; 
		do 
			n=(n*10.0)+(*num++ -'0'),scale--; 
		while (*num>='0' && *num<='9');
	}	/* Fractional part? */
	if (*num=='e' || *num=='E')		/* Exponent? */
	{	num++;if (*num=='+') num++;	else if (*num=='-') signsubscale=-1,num++;		/* With sign? */
		while (*num>='0' && *num<='9') subscale=(subscale*10)+(*num++ - '0');	/* Number? */
	}

	n=sign*n*pow(10.0,(scale+subscale*signsubscale));	/* number = +/- number.fraction * 10^+/- exponent */
	
	item->mValueDouble=n;
	item->mValueInt=(int)n;
	item->mType=Json::Type_Number;
	return num;
}

/* Render the number nicely from the given item into a string. */
static char *print_number(Json* item)
{
	char *str;
	double d=item->mValueDouble;
	if (fabs(((double)item->mValueInt)-d)<=DBL_EPSILON && d<=INT_MAX && d>=INT_MIN)
	{
		str=(char*)Json::malloc(21);	/* 2^64+1 can be represented in 21 chars. */
		if (str) sprintf(str,"%d",item->mValueInt);
	}
	else
	{
		str=(char*)Json::malloc(64);	/* This is a nice tradeoff. */
		if (str)
		{
			if (fabs(floor(d)-d)<=DBL_EPSILON && fabs(d)<1.0e60)sprintf(str,"%.0f",d);
			else if (fabs(d)<1.0e-6 || fabs(d)>1.0e9)			sprintf(str,"%e",d);
			else												sprintf(str,"%f",d);
		}
	}
	return str;
}

static unsigned parse_hex4(const char *str)
{
	unsigned h=0;
	if (*str>='0' && *str<='9') h+=(*str)-'0'; else if (*str>='A' && *str<='F') h+=10+(*str)-'A'; else if (*str>='a' && *str<='f') h+=10+(*str)-'a'; else return 0;
	h=h<<4;str++;
	if (*str>='0' && *str<='9') h+=(*str)-'0'; else if (*str>='A' && *str<='F') h+=10+(*str)-'A'; else if (*str>='a' && *str<='f') h+=10+(*str)-'a'; else return 0;
	h=h<<4;str++;
	if (*str>='0' && *str<='9') h+=(*str)-'0'; else if (*str>='A' && *str<='F') h+=10+(*str)-'A'; else if (*str>='a' && *str<='f') h+=10+(*str)-'a'; else return 0;
	h=h<<4;str++;
	if (*str>='0' && *str<='9') h+=(*str)-'0'; else if (*str>='A' && *str<='F') h+=10+(*str)-'A'; else if (*str>='a' && *str<='f') h+=10+(*str)-'a'; else return 0;
	return h;
}

/* Parse the input text into an unescaped cstring, and populate item. */
static const unsigned char firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };
static const char *parse_string(Json* item,const char *str)
{
	const char *ptr=str+1;char *ptr2;char *out;int len=0;unsigned uc,uc2;
	if (*str!='\"') {ep=str;return 0;}	/* not a string! */
	
	while (*ptr!='\"' && *ptr && ++len) if (*ptr++ == '\\') ptr++;	/* Skip escaped quotes. */
	
	out=(char*)Json::malloc(len+1);	/* This is how long we need for the string, roughly. */
	if (!out) return 0;
	
	ptr=str+1;ptr2=out;
	while (*ptr!='\"' && *ptr)
	{
		if (*ptr!='\\') *ptr2++=*ptr++;
		else
		{
			ptr++;
			switch (*ptr)
			{
				case 'b': *ptr2++='\b';	break;
				case 'f': *ptr2++='\f';	break;
				case 'n': *ptr2++='\n';	break;
				case 'r': *ptr2++='\r';	break;
				case 't': *ptr2++='\t';	break;
				case 'u':	 /* transcode utf16 to utf8. */
					uc=parse_hex4(ptr+1);ptr+=4;	/* get the unicode char. */

					if ((uc>=0xDC00 && uc<=0xDFFF) || uc==0)	break;	/* check for invalid.	*/

					if (uc>=0xD800 && uc<=0xDBFF)	/* UTF16 surrogate pairs.	*/
					{
						if (ptr[1]!='\\' || ptr[2]!='u')	break;	/* missing second-half of surrogate.	*/
						uc2=parse_hex4(ptr+3);ptr+=6;
						if (uc2<0xDC00 || uc2>0xDFFF)		break;	/* invalid second-half of surrogate.	*/
						uc=0x10000 + (((uc&0x3FF)<<10) | (uc2&0x3FF));
					}

					len=4;if (uc<0x80) len=1;else if (uc<0x800) len=2;else if (uc<0x10000) len=3; ptr2+=len;
					
					switch (len) {
						case 4: *--ptr2 =((uc | 0x80) & 0xBF); uc >>= 6;
						case 3: *--ptr2 =((uc | 0x80) & 0xBF); uc >>= 6;
						case 2: *--ptr2 =((uc | 0x80) & 0xBF); uc >>= 6;
						case 1: *--ptr2 =(uc | firstByteMark[len]);
					}
					ptr2+=len;
					break;
				default:  *ptr2++=*ptr; break;
			}
			ptr++;
		}
	}
	*ptr2=0;
	if (*ptr=='\"') ptr++;
	item->mValueString=out;
	item->mType=Json::Type_String;
	return ptr;
}

/* Render the cstring provided to an escaped version that can be printed. */
static char *print_string_ptr(const char *str)
{
	const char *ptr;char *ptr2,*out;int len=0;unsigned char token;
	
	if (!str) return JSON_strdup("");
	ptr=str;while ((token=*ptr) && ++len) {if (strchr("\"\\\b\f\n\r\t",token)) len++; else if (token<32) len+=5;ptr++;}
	
	out=(char*)Json::malloc(len+3);
	if (!out) return 0;

	ptr2=out;ptr=str;
	*ptr2++='\"';
	while (*ptr)
	{
		if ((unsigned char)*ptr>31 && *ptr!='\"' && *ptr!='\\') *ptr2++=*ptr++;
		else
		{
			*ptr2++='\\';
			switch (token=*ptr++)
			{
				case '\\':	*ptr2++='\\';	break;
				case '\"':	*ptr2++='\"';	break;
				case '\b':	*ptr2++='b';	break;
				case '\f':	*ptr2++='f';	break;
				case '\n':	*ptr2++='n';	break;
				case '\r':	*ptr2++='r';	break;
				case '\t':	*ptr2++='t';	break;
				default: sprintf(ptr2,"u%04x",token);ptr2+=5;	break;	/* escape and print */
			}
		}
	}
	*ptr2++='\"';*ptr2++=0;
	return out;
}
/* Invote print_string_ptr (which is useful) on an item. */
static char *print_string(Json* item)	{return print_string_ptr(item->mValueString);}

/* Predeclare these prototypes. */
static const char *parse_value(Json* item,const char *value);
static char *print_value(Json* item,int depth,int fmt);
static const char *parse_array(Json* item,const char *value);
static char *print_array(Json* item,int depth,int fmt);
static const char *parse_object(Json* item,const char *value);
static char *print_object(Json* item,int depth,int fmt);

/* Utility to jump whitespace and cr/lf */
static const char *skip(const char *in) {while (in && *in && (unsigned char)*in<=32) in++; return in;}

/* Parse an object - create a new root, and populate. */
Json* Json::ParseWithOpts(const char *value,const char **return_parse_end,int require_null_terminated)
{
	const char *end=0;
	Json* c=New_Item();
	ep=0;
	if (!c) return 0;       /* memory fail */

	end=parse_value(c,skip(value));
	if (!end)	{delete(c);return 0;}	/* parse failure. ep is set. */

	/* if we require null-terminated JSON without appended garbage, skip and then check for a null terminator */
	if (require_null_terminated) {end=skip(end);if (*end) {delete(c);ep=end;return 0;}}
	if (return_parse_end) *return_parse_end=end;
	return c;
}
/* Default options for Json::Parse */
Json* Json::Parse(const char *value) {return Json::ParseWithOpts(value,0,0);}

/* Render a cJSON item/entity/structure to text. */
char *Json::Print()				{return print_value(this,0,1);}
char *Json::PrintUnformatted()	{return print_value(this,0,0);}

/* Parser core - when encountering text, process appropriately. */
static const char *parse_value(Json* item,const char *value)
{
	//item->mSrcStart = value;

	if (!value)						return 0;	/* Fail on null. */
	if (!strncmp(value,"null",4))	{ item->mType=Json::Type_NULL;  return value+4; }
	if (!strncmp(value,"false",5))	{ item->mType=Json::Type_False; return value+5; }
	if (!strncmp(value,"true",4))	{ item->mType=Json::Type_True; item->mValueInt=1;	return value+4; }
	if (*value=='\"')				{ return parse_string(item,value); }
	if (*value=='-' || (*value>='0' && *value<='9'))	{ return parse_number(item,value); }
	if (*value=='[')				{ return parse_array(item,value); }
	if (*value=='{')				{ return parse_object(item,value); }

	ep=value;return 0;	/* failure. */
}

/* Render a value to text. */
static char *print_value(Json* item,int depth,int fmt)
{
	char *out=0;
	if (!item) return 0;
	switch ((item->mType)&255)
	{
		case Json::Type_NULL:	out=JSON_strdup("null");	break;
		case Json::Type_False:	out=JSON_strdup("false");break;
		case Json::Type_True:	out=JSON_strdup("true"); break;
		case Json::Type_Number:	out=print_number(item);break;
		case Json::Type_String:	out=print_string(item);break;
		case Json::Type_Array:	out=print_array(item,depth,fmt);break;
		case Json::Type_Object:	out=print_object(item,depth,fmt);break;
	}
	return out;
}

/* Build an array from input text. */
static const char *parse_array(Json* item,const char *value)
{
	Json* child;
	if (*value!='[')	{ep=value;return 0;}	/* not an array! */
	
	item->mType=Json::Type_Array;
	value=skip(value+1);
	if (*value==']') return value+1;	/* empty array. */

	item->mChild=child=New_Item();
	if (!item->mChild) return 0;		 /* memory fail */
	value = skip(value);
	child->mSrcStart = value;
	value=skip(parse_value(child,value));	/* skip any spacing, get the value. */
	if (!value) return 0;

	while (*value==',')
	{
		Json* new_item;
		if (!(new_item=New_Item())) return 0; 	/* memory fail */
		child->mNext=new_item;new_item->mPrev=child;child=new_item;		
		value = skip(value+1);
		child->mSrcStart = value;
		value=skip(parse_value(child,value));
		if (!value) return 0;	/* memory fail */
	}

	if (*value==']') return value+1;	/* end of array */
	ep=value;return 0;	/* malformed. */
}

/* Render an array to text */
static char *print_array(Json* item,int depth,int fmt)
{
	char **entries;
	char *out=0,*ptr,*ret;int len=5;
	Json* child=item->mChild;
	int numentries=0,i=0,fail=0;
	
	/* How many entries in the array? */
	while (child) numentries++,child=child->mNext;
	/* Explicitly handle numentries==0 */
	if (!numentries)
	{
		out=(char*)Json::malloc(3);
		if (out) strcpy(out,"[]");
		return out;
	}
	/* Allocate an array to hold the values for each */
	entries=(char**)Json::malloc(numentries*sizeof(char*));
	if (!entries) return 0;
	memset(entries,0,numentries*sizeof(char*));
	/* Retrieve all the results: */
	child=item->mChild;
	while (child && !fail)
	{
		ret=print_value(child,depth+1,fmt);
		entries[i++]=ret;
		if (ret) len+=(int)strlen(ret)+2+(fmt?1:0); else fail=1;
		child=child->mNext;
	}
	
	/* If we didn't fail, try to malloc the output string */
	if (!fail) out=(char*)Json::malloc(len);
	/* If that fails, we fail. */
	if (!out) fail=1;

	/* Handle failure. */
	if (fail)
	{
		for (i=0;i<numentries;i++) if (entries[i]) Json::free(entries[i]);
		Json::free(entries);
		return 0;
	}
	
	/* Compose the output array. */
	*out='[';
	ptr=out+1;*ptr=0;
	for (i=0;i<numentries;i++)
	{
		strcpy(ptr,entries[i]);ptr+=strlen(entries[i]);
		if (i!=numentries-1) {*ptr++=',';if(fmt)*ptr++=' ';*ptr=0;}
		Json::free(entries[i]);
	}
	Json::free(entries);
	*ptr++=']';*ptr++=0;
	return out;	
}

/* Build an object from the text. */
static const char *parse_object(Json* item,const char *value)
{
	Json* child;
	if (*value!='{')	{ep=value;return 0;}	/* not an object! */
	
	item->mType=Json::Type_Object;
	value=skip(value+1);
	if (*value=='}') return value+1;	/* empty array. */
	
	item->mChild=child=New_Item();
	value = skip(value);
	child->mSrcStart = value;
	if (!item->mChild) return 0;
	value=skip(parse_string(child,value));
	if (!value) return 0;
	child->mName=child->mValueString;child->mValueString=0;
	if (*value!=':') {ep=value;return 0;}	/* fail! */
	value=skip(parse_value(child,skip(value+1)));	/* skip any spacing, get the value. */
	if (!value) return 0;
	
	while (*value==',')
	{
		Json* new_item;
		if (!(new_item=New_Item()))	return 0; /* memory fail */
		child->mNext=new_item;new_item->mPrev=child;child=new_item;
		value = skip(value+1);
		child->mSrcStart = value;
		value=skip(parse_string(child,value));
		if (!value) return 0;
		child->mName=child->mValueString;child->mValueString=0;
		if (*value!=':') {ep=value;return 0;}	/* fail! */
		value=skip(parse_value(child,skip(value+1)));	/* skip any spacing, get the value. */
		if (!value) return 0;
	}
	
	child->mSrcEnd = value;

	if (*value=='}') return value+1;	/* end of array */
	ep=value;return 0;	/* malformed. */
}

/* Render an object to text. */
static char *print_object(Json* item,int depth,int fmt)
{
	char **entries=0,**names=0;
	char *out=0,*ptr,*ret,*str;int len=7,i=0,j;
	Json* child=item->mChild;
	int numentries=0,fail=0;
	/* Count the number of entries. */
	while (child) numentries++,child=child->mNext;
	/* Explicitly handle empty object case */
	if (!numentries)
	{
		out=(char*)Json::malloc(fmt?depth+4:3);
		if (!out)	return 0;
		ptr=out;*ptr++='{';
		if (fmt) {*ptr++='\n';for (i=0;i<depth-1;i++) *ptr++='\t';}
		*ptr++='}';*ptr++=0;
		return out;
	}
	/* Allocate space for the names and the objects */
	entries=(char**)Json::malloc(numentries*sizeof(char*));
	if (!entries) return 0;
	names=(char**)Json::malloc(numentries*sizeof(char*));
	if (!names) {Json::free(entries);return 0;}
	memset(entries,0,sizeof(char*)*numentries);
	memset(names,0,sizeof(char*)*numentries);

	/* Collect all the results into our arrays: */
	child=item->mChild;depth++;if (fmt) len+=depth;
	while (child)
	{
		names[i]=str=print_string_ptr(child->mName);
		entries[i++]=ret=print_value(child,depth,fmt);
		if (str && ret) len+= (int)strlen(ret)+ (int)strlen(str)+2+(fmt?2+depth:0); else fail=1;
		child=child->mNext;
	}
	
	/* Try to allocate the output string */
	if (!fail) out=(char*)Json::malloc(len);
	if (!out) fail=1;

	/* Handle failure */
	if (fail)
	{
		for (i=0;i<numentries;i++) {if (names[i]) Json::free(names[i]);if (entries[i]) Json::free(entries[i]);}
		Json::free(names);Json::free(entries);
		return 0;
	}
	
	/* Compose the output: */
	*out='{';ptr=out+1;if (fmt)*ptr++='\n';*ptr=0;
	for (i=0;i<numentries;i++)
	{
		if (fmt) for (j=0;j<depth;j++) *ptr++='\t';
		strcpy(ptr,names[i]);ptr+=(int)strlen(names[i]);
		*ptr++=':';if (fmt) *ptr++='\t';
		strcpy(ptr,entries[i]);ptr+=strlen(entries[i]);
		if (i!=numentries-1) *ptr++=',';
		if (fmt) *ptr++='\n';*ptr=0;
		Json::free(names[i]);Json::free(entries[i]);
	}
	
	Json::free(names);Json::free(entries);
	if (fmt) for (i=0;i<depth-1;i++) *ptr++='\t';
	*ptr++='}';*ptr++=0;
	return out;	
}

/* Get Array size/item / object item. */
int Json::GetArraySize()							{Json* c=this->mChild;int i=0;while(c)i++,c=c->mNext;return i;}
Json* Json::GetArrayItem(int item)				{Json* c=this->mChild;  while (c && item>0) item--,c=c->mNext; return c;}
Json* Json::GetObjectItem(const char *string)	{Json* c=this->mChild; while (c && JSON_strcasecmp(c->mName,string)) c=c->mNext; return c;}

/* Utility for array list handling. */
static void suffix_object(Json* prev,Json* item) {prev->mNext=item;item->mPrev=prev;}
/* Utility for handling references. */
static Json* create_reference(Json* item) {Json* ref=New_Item();if (!ref) return 0;memcpy(ref,item,sizeof(Json));ref->mName=0;ref->mType=(Json::Type)(ref->mType|Json::Type_IsReference);ref->mNext=ref->mPrev=0;return ref;}

/* Add item to array/object. */
void Json::AddItemToArray(Json* item)						{Json* c=this->mChild;if (!item) return; if (!c) {this->mChild=item;} else {while (c && c->mNext) c=c->mNext; suffix_object(c,item);}}
void Json::AddItemToObject(const char *string,Json* item)	{if (!item) return; if (item->mName) Json::free(item->mName);item->mName=JSON_strdup(string);this->AddItemToArray(item);}
void Json::AddItemReferenceToArray(Json* item)						{this->AddItemToArray(create_reference(item));}
void Json::AddItemReferenceToObject(const char *string,Json* item)	{this->AddItemToObject(string,create_reference(item));}

Json* Json::DetachItemFromArray(Json* array,int which)			{Json* c=array->mChild;while (c && which>0) c=c->mNext,which--;if (!c) return 0;
	if (c->mPrev) c->mPrev->mNext=c->mNext;if (c->mNext) c->mNext->mPrev=c->mPrev;if (c==array->mChild) array->mChild=c->mNext;c->mPrev=c->mNext=0;return c;}
void   deleteItemFromArray(Json* array,int which)			{delete(Json::DetachItemFromArray(array,which));}
Json* Json::DetachItemFromObject(Json* object,const char *string) {int i=0;Json* c=object->mChild;while (c && JSON_strcasecmp(c->mName,string)) i++,c=c->mNext;if (c) return Json::DetachItemFromArray(object,i);return 0;}
void   deleteItemFromObject(Json* object,const char *string) {delete(Json::DetachItemFromObject(object,string));}

/* Replace array/object items with new ones. */
void   Json::ReplaceItemInArray(Json* array,int which,Json* newitem)		{Json* c=array->mChild;while (c && which>0) c=c->mNext,which--;if (!c) return;
	newitem->mNext=c->mNext;newitem->mPrev=c->mPrev;if (newitem->mNext) newitem->mNext->mPrev=newitem;
	if (c==array->mChild) array->mChild=newitem; else newitem->mPrev->mNext=newitem;c->mNext=c->mPrev=0;delete(c);}
void   Json::ReplaceItemInObject(Json* object,const char *string,Json* newitem){int i=0;Json* c=object->mChild;while(c && JSON_strcasecmp(c->mName,string))i++,c=c->mNext;if(c){newitem->mName=JSON_strdup(string);Json::ReplaceItemInArray(object,i,newitem);}}

/* Create basic types: */
Json* Json::CreateNull(void)					{Json* item=New_Item();if(item)item->mType=Json::Type_NULL;return item;}
Json* Json::CreateTrue(void)					{Json* item=New_Item();if(item)item->mType=Json::Type_True;return item;}
Json* Json::CreateFalse(void)					{Json* item=New_Item();if(item)item->mType=Json::Type_False;return item;}
Json* Json::CreateBool(int b)					{Json* item=New_Item();if(item)item->mType=b?Json::Type_True:Json::Type_False;return item;}
Json* Json::CreateNumber(double num)			{Json* item=New_Item();if(item){item->mType=Json::Type_Number;item->mValueDouble=num;item->mValueInt=(int)num;}return item;}
Json* Json::CreateString(const char *string)	{Json* item=New_Item();if(item){item->mType=Json::Type_String;item->mValueString=JSON_strdup(string);}return item;}
Json* Json::CreateArray(void)					{Json* item=New_Item();if(item)item->mType=Json::Type_Array;return item;}
Json* Json::CreateObject(void)					{Json* item=New_Item();if(item)item->mType=Json::Type_Object;return item;}

/* Create Arrays: */
Json* Json::CreateIntArray(const int *numbers,int count)		{int i;Json* n=0,*p=0,*a=Json::CreateArray();for(i=0;a && i<count;i++){n=Json::CreateNumber(numbers[i]);if(!i)a->mChild=n;else suffix_object(p,n);p=n;}return a;}
Json* Json::CreateFloatArray(const float *numbers,int count)	{int i;Json* n=0,*p=0,*a=Json::CreateArray();for(i=0;a && i<count;i++){n=Json::CreateNumber(numbers[i]);if(!i)a->mChild=n;else suffix_object(p,n);p=n;}return a;}
Json* Json::CreateDoubleArray(const double *numbers,int count)	{int i;Json* n=0,*p=0,*a=Json::CreateArray();for(i=0;a && i<count;i++){n=Json::CreateNumber(numbers[i]);if(!i)a->mChild=n;else suffix_object(p,n);p=n;}return a;}
Json* Json::CreateStringArray(const char **strings,int count)	{int i;Json* n=0,*p=0,*a=Json::CreateArray();for(i=0;a && i<count;i++){n=Json::CreateString(strings[i]);if(!i)a->mChild=n;else suffix_object(p,n);p=n;}return a;}

/* Duplication */
Json* Json::Duplicate(Json* item,int recurse)
{
	Json* newitem,*cptr,*nptr=0,*newchild;
	/* Bail on bad ptr */
	if (!item) return 0;
	/* Create new item */
	newitem=New_Item();
	if (!newitem) return 0;
	/* Copy over all vars */
	newitem->mType=(Json::Type)(item->mType&(~Type_IsReference)),newitem->mValueInt=item->mValueInt,newitem->mValueDouble=item->mValueDouble;
	if (item->mValueString)	{newitem->mValueString=JSON_strdup(item->mValueString);	if (!newitem->mValueString)	{delete(newitem);return 0;}}
	if (item->mName)		{newitem->mName=JSON_strdup(item->mName);			if (!newitem->mName)		{delete(newitem);return 0;}}
	/* If non-recursive, then we're done! */
	if (!recurse) return newitem;
	/* Walk the ->mNext chain for the child. */
	cptr=item->mChild;
	while (cptr)
	{
		newchild=Json::Duplicate(cptr,1);		/* Duplicate (with recurse) each item in the ->mNext chain */
		if (!newchild) {delete(newitem);return 0;}
		if (nptr)	{nptr->mNext=newchild,newchild->mPrev=nptr;nptr=newchild;}	/* If newitem->mChild already set, then crosswire ->mPrev and ->mNext and move on */
		else		{newitem->mChild=newchild;nptr=newchild;}					/* Set newitem->mChild and move to it */
		cptr=cptr->mNext;
	}
	return newitem;
}

void Json::Minify(char *json)
{
	char *into=json;
	while (*json)
	{
		if (*json==' ') json++;
		else if (*json=='\t') json++;	// Whitespace characters.
		else if (*json=='\r') json++;
		else if (*json=='\n') json++;
		else if (*json=='/' && json[1]=='/')  while (*json && *json!='\n') json++;	// double-slash comments, to end of line.
		else if (*json=='/' && json[1]=='*') {while (*json && !(*json=='*' && json[1]=='/')) json++;json+=2;}	// multiline comments.
		else if (*json=='\"'){*into++=*json++;while (*json && *json!='\"'){if (*json=='\\') *into++=*json++;*into++=*json++;}*into++=*json++;} // string literals, which are \" sensitive.
		else *into++=*json++;			// All other characters.
	}
	*into=0;	// and null-terminate.
}
