from dumper import *

def qdump__Beefy__String(d, value):
	qdump__Beefy__StringImpl(d, value)

def qdump__Beefy__StringImpl(d, value):
	if (int(value["mAllocSizeAndFlags"]) & 0x40000000) == 0:
		d.putCharArrayHelper(d.addressOf(value["mPtr"]), value["mLength"], 1)
	else:		
		d.putCharArrayHelper(value["mPtr"], value["mLength"], 1)
	d.putNumChild(2)
	if d.isExpanded():
		with Children(d):
			d.putIntItem("[Length]", value["mLength"])
			d.putIntItem("[Size]", int(value["mAllocSizeAndFlags"]) & 0x3FFFFFFF)

def qdump__Beefy__Array(d, value):	
	numItems = int(value["mSize"])
	d.putValue("Size=" + str(numItems))
	d.putNumChild(int(numItems) + 2)	
	if d.isExpanded():
		with Children(d):
			d.putIntItem("[Size]", value["mSize"])
			d.putIntItem("[AllocSize]", value["mSize"])
			for i in range(0, int(numItems)):				
				d.putSubItem("[" + str(i) + "]", value["mVals"] + i)

def qdump__Beefy__BfSizedArray(d, value):	
	numItems = int(value["mSize"])
	d.putValue("Size=" + str(numItems))
	d.putNumChild(int(numItems) + 1)
	if d.isExpanded():
		with Children(d):
			d.putIntItem("[Size]", value["mSize"])			
			for i in range(0, int(numItems)):				
				d.putSubItem("[" + str(i) + "]", value["mVals"] + i)

def qdump__Beefy__BfAstNode(d, value):
	d.putCharArrayHelper(value["mSource"]["mSrc"] + int(value["mSrcStart"]), int(value["mSrcEnd"]) - int(value["mSrcStart"]), 1)
	d.putPlainChildren(value)

def qdump__System__String(d, value):
	if (int(value["mAllocSizeAndFlags"]) & 0x40000000) == 0:
		d.putCharArrayHelper(d.addressOf(value["mPtr"]), value["mLength"], 1)
	else:		
		d.putCharArrayHelper(value["mPtr"], value["mLength"], 1)
	d.putNumChild(2)
	if d.isExpanded():
		with Children(d):
			d.putIntItem("[Length]", value["mLength"])
			d.putIntItem("[Size]", int(value["mAllocSizeAndFlags"]) & 0x3FFFFFFF)			

def qdump__System__Collections__Generic__List(d, value):	
	numItems = int(value["mSize"])
	d.putValue("Size=" + str(numItems))
	d.putNumChild(int(numItems) + 2)	
	if d.isExpanded():
		with Children(d):
			d.putIntItem("[Size]", value["mSize"])
			d.putIntItem("[AllocSize]", value["mSize"])
			for i in range(0, int(numItems)):				
				d.putSubItem("[" + str(i) + "]", value["mVals"] + i)