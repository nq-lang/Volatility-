import os as _os; VT_ROOT = _os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".."))
# Builds preview.html straight from the PRODUCTION web/index.html (same markup, same JS/CSS files
# referenced in place via ../web/) -- only the network CDN tags are dropped (no network here).
import re
s = open((VT_ROOT + "/web/index.html")).read()
s = re.sub(r'<script src="https://[^\n]*</script>\n', '', s)
s = s.replace('href="css/', 'href="../web/css/').replace('src="js/', 'src="../web/js/')
open((VT_ROOT + "/preview/preview.html"), "w").write(s)
print("preview.html built from web/index.html")
