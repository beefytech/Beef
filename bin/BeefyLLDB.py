""" File: BeefyLLDB.py """
import lldb
import shlex

class BFArraySyntheticChildrenProvider:
	def __init__(self, valobj, internal_dict):
		#print "Var: " + valobj.GetName()
		self.startIdx = 1
		self.obj = valobj
		self.update()
	def update(self):
		self.length = self.obj.GetChildMemberWithName("mLength")
		self.firstElement = self.obj.GetChildMemberWithName("mFirstElement")
		self.dataType = self.firstElement.GetType()
	def num_children(self): 
		return min(1024, self.length.GetValueAsSigned()) + self.startIdx;
	def get_child_index(self,name): 
		if (name == "mLength"):
			return 0;
		try:
			return int(name.lstrip('[').rstrip(']')) + self.startIdx
		except:
			return -1
	def get_child_at_index(self,index): 
		if index < 0:
			return None;
		if index >= self.num_children():
			return None;
		
		if (index == 0):
			return self.length;

		offset = (index - self.startIdx) * self.dataType.GetByteSize()
		return self.firstElement.AddressOf().CreateChildAtOffset('['+str(index - self.startIdx)+']', offset, self.dataType)

	def has_children(self): 
		return True

def StringSummaryProvider(valobj, dict):
	e = lldb.SBError()
	s = u'"'
	
	if valobj.GetValue() != 0:
		strObj = valobj.GetChildMemberWithName("start_char")
		len = valobj.GetChildMemberWithName("length").GetValueAsSigned()
		
		i = 0
		newchar = -1
		ptrType = strObj.GetType().GetPointerType()

		for i in range(0, min(8192, len)):
			data_val = strObj.CreateChildAtOffset('['+str(i)+']', i*2, strObj.GetType())
			newchar = data_val.GetValueAsUnsigned()
			i = i + 1
			
			if newchar != 0:
				s = s + unichr(newchar)
		s = s + u'"'
	return s.encode('utf-8')

def __lldb_init_module(debugger,dict):
	print "InitModule"
	debugger.HandleCommand('type summary add --expand -x "BFArray1<" -summary-string "len = ${var.mLength}"')
	debugger.HandleCommand('type synthetic add -x "BFArray1<" -l BeefyLLDB.BFArraySyntheticChildrenProvider')
	debugger.HandleCommand('type summary add "BFType" -summary-string "${var.mDebugTypeData->mName}@${var.mDebugTypeData->mNamespace}"')
	debugger.HandleCommand('type summary add "BFTypeRoot" -summary-string "${var.mDebugTypeData->mName}@${var.mDebugTypeData->mNamespace}"')
	debugger.HandleCommand('type summary add "System::String" -F BeefyLLDB.StringSummaryProvider')
