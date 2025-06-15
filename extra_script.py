Import("env")

def before_build(source, target, env):
    print("Running pre-build script...")
    # Execute the pre_build.py script
    cmd = "python3 $PROJECT_DIR/pre_build.py"
    print(f"Executing: {cmd}")
    return env.Execute(cmd)

# Add the pre-build action
env.AddPreAction("buildprog", before_build)
