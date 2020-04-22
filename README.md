# SymRez
When dlsym isn't enough

## When to use?
Although dlsym is very powerful, it usually plays by the rules and will not resolve non-exported symbols. This is where SymRez comes into play. SymRez works by manually resolving symbol names to their pointer locations in the symbol table inside Mach-O files. Works especially well for hooking symbolicated global variables, which dlsym will not :) 

Note: SymRez does not demangle symbols. The raw symbol name is required for this to work.

## Example
```
void* (*__CGSWindowByID)(int windowID);
void* (*__BunldeInfo)(const CFURLRef bundleURL);

...

symrez_t skylight = symrez_new(“SkyLight”);
if(skylight != NULL) {
	__CGSWindowByID = sr_resolve_symbol(skylight, "_CGSWindowByID");

symrez_t launchservices = symrez_new(“LaunchServices”);
if(launchservices != NULL) {
	__BundleInfo = sr_resolve_symbol(LaunchServices, "__ZN10BundleInfoC2EPK7__CFURL");
```
