"""Build runner for XRS Gomoku (五子棋人机对弈)"""
import os
import sys
import subprocess

os.environ['IDF_PATH'] = r'C:\Users\ThinkPad\esp-idf-v5.4'
os.environ['IDF_PYTHON_ENV_PATH'] = r'C:\Users\ThinkPad\.espressif\python_env\idf5.4_py3.12_env'

project_dir = os.path.dirname(os.path.abspath(__file__))
idf_py = os.path.join(os.environ['IDF_PATH'], 'tools', 'idf.py')

os.chdir(project_dir)

def run_idf(args):
    cmd = [sys.executable, idf_py] + args
    print(f"\n=== Running: {' '.join(cmd)} ===")
    result = subprocess.run(cmd, capture_output=False, text=True)
    if result.returncode != 0:
        print(f"FAILED with code {result.returncode}")
        sys.exit(result.returncode)

# run_idf(['set-target', 'esp32'])  # skip — already configured
run_idf(['build'])
print("\n=== BUILD SUCCESS ===")
