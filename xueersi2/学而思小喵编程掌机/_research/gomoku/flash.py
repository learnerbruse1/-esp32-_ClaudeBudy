"""Flash the Gomoku firmware to XRS Coding Machine"""
import os
import sys
import subprocess

os.environ['IDF_PATH'] = r'C:\Users\ThinkPad\esp-idf-v5.4'

project_dir = os.path.dirname(os.path.abspath(__file__))
idf_py = os.path.join(os.environ['IDF_PATH'], 'tools', 'idf.py')

os.chdir(project_dir)

cmd = [sys.executable, idf_py, '-p', 'COM14', 'flash']
print(f"Running: {' '.join(cmd)}")
result = subprocess.run(cmd)
sys.exit(result.returncode)
