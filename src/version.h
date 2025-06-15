#ifndef VERSION_H
#define VERSION_H

// Git SHA will be passed by the build system
#ifndef GIT_SHA
  #define GIT_SHA unknown
#else
  // Convert GIT_SHA to a string literal
  #define STRINGIFY(s) #s
  #define XSTRINGIFY(s) STRINGIFY(s)
  #define GIT_SHA_STR XSTRINGIFY(GIT_SHA)
#endif

// Build timestamp using compiler macros
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

// For backward compatibility
#define BUILD_TIMESTAMP (String(BUILD_DATE) + " " + String(BUILD_TIME))

// Function to get the git SHA as a String
String getGitSha() {
  #ifdef GIT_SHA_STR
    return String(GIT_SHA_STR);
  #else
    return String("unknown");
  #endif
}

#endif // VERSION_H
