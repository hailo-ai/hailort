import re

def function(text,name):
    match=re.search(r'(?m)^(?:static )?[^\n;]*\b'+name+r'\([^;]*?\)\s*\{',text)
    if not match:raise ValueError('missing function: '+name)
    depth=0
    for token in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',text[match.end()-1:],re.S):
        if token.group()=='{':depth+=1
        elif token.group()=='}':
            depth-=1
            if not depth:return text[match.start():match.end()-1+token.end()]
    raise ValueError('unbalanced function: '+name)
