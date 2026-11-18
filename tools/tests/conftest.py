import sys
from pathlib import Path

# Make `import xrdome_tools` work however pytest is started.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
