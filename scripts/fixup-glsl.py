import sys
import re

def processline(line, regexp):
   stripped_line = line.strip(' ')
   m = regexp.match(stripped_line)
   inc_lines = [line]
   if m:
      s = m.span()
      inc_filename = stripped_line[s[0]+8:s[1]-1]
      inc_lines = open(inc_filename).readlines()
   return inc_lines

in_filename = sys.argv[1]
out_filename = sys.argv[2]
regexp = re.compile('INCLUDE(.*)')
new_lines = []

f = open(in_filename);
for line in f:
   new_lines += processline(line, regexp)
f.close()

f = open(out_filename, 'w')
for line in new_lines:
   f.write(line)
f.close()
