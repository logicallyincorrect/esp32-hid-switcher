"""NVS rejects key names longer than 15 characters (NVS_KEY_NAME_MAX_SIZE 16)."""
from pathlib import Path
import re
keys=set()
for path in Path('src').glob('*.[ch]*'):
 text=path.read_text()
 keys.update(re.findall(r'_prefs\.\w+\("([^"]+)"',text))
 keys.update(re.findall(r'constexpr char \w+Key\[\]="([^"]+)"',text))
assert 'preset-keys' in keys and 'shortcuts' in keys,sorted(keys)
long=[k for k in keys if len(k)>15]
assert not long,f'NVS keys longer than 15 characters: {long}'
print('NVS key lengths passed')
