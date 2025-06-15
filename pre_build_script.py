import subprocess
import os

def get_git_sha():
    try:
        # Get the short Git SHA
        sha = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], 
                                   stderr=subprocess.STDOUT).decode('ascii').strip()
        return sha
    except (subprocess.SubprocessError, OSError) as e:
        print(f"Error getting Git SHA: {e}")
        return "unknown"

def main():
    git_sha = get_git_sha()
    # Set the GIT_SHA environment variable for PlatformIO
    os.environ["GIT_SHA"] = git_sha
    print(f"Setting GIT_SHA={git_sha}")

if __name__ == "__main__":
    main()
