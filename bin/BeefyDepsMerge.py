# Sublime Text 3 macro to merge CodeConv deps files
#  Open deps files and run this macro.  A new file will be created that
#  includes data from all open deps files.
import sublime, sublime_plugin  
  
class Beefy_deps_mergeCommand(sublime_plugin.TextCommand):  
    def run(self, edit):
        nv = self.view.window().new_file()

        typeMap = {};
        strOut = "{\n\t\"trimReflection\":true,\n\t\"requiredTypes\":\n\t[\n"

        for view in self.view.window().views():         
            fileName = view.file_name()         
            if ((fileName == None) or (fileName.find(".bfrtproj") == -1)):
                continue        

            viewRegion = sublime.Region(0, view.size())
            fullText = view.substr(viewRegion)
            lines = fullText.splitlines()
            
            for line in lines:
                if not line.startswith("\t\t\""):
                    continue

                dataStr = line.split('\"')[1]
                parts = dataStr.split(':')

                typeName = parts[0]
                memberData = parts[1].split(';')

                if not typeName in typeMap:                    
                    typeMap[typeName] = []

                for memberName in memberData:
                    if (not memberName in typeMap[typeName]):
                        typeMap[typeName].append(memberName)                        

        for typeName in sorted(typeMap):
            lineOut = "\t\t\"" + typeName + ":"
            lineOut += ";".join(sorted(typeMap[typeName]))
            lineOut += "\",\n"
            strOut += lineOut

        strOut += "\t]\n}\n"

        nv.replace(edit, sublime.Region(nv.size(), nv.size()), strOut)            
