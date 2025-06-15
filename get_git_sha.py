import os
import subprocess
import sys

def get_git_sha():
    try:
        # First try to get the SHA from git
        sha = subprocess.check_output(
            ['git', 'rev-parse', '--short=7', 'HEAD'],
            stderr=subprocess.PIPE
        ).decode('ascii').strip()
        return sha
    except (subprocess.SubprocessError, OSError) as e:
        print(f"Error getting git SHA: {e}", file=sys.stderr)
        # Fallback: if .git_sha file exists, use that
        if os.path.exists('.git_sha'):
            try:
                with open('.git_sha', 'r') as f:
                    return f.read().strip()
            except Exception as e:
                print(f"Error reading .git_sha: {e}", file=sys.stderr)
        return 'unknown'

def main():
    # Get Git SHA
    git_sha = get_git_sha()
    
    # Print the Git SHA to be captured by PlatformIO
    print(f'GIT_SHA={git_sha}')
    
    # Also write it to a file for reference
    try:
        with open('.git_sha', 'w') as f:
            f.write(git_sha)
    except Exception as e:
        print(f"Error writing .git_sha: {e}", file=sys.stderr)

if __name__ == '__main__':
    main()
