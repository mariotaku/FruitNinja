#ifndef FN_HUD_LOCALIZED_STRING_H
#define FN_HUD_LOCALIZED_STRING_H

// LocalizedString -- port stub for the binary's BakedString-based localised
// string type used by CheckBox and SliderControl overloads.
// Full RE is not yet complete; this minimal wrapper lets the overload compile.
struct LocalizedString {
    const char* str;
    explicit LocalizedString(const char* s) : str(s) {}
    operator const char*() const { return str; }
};

#endif // FN_HUD_LOCALIZED_STRING_H
