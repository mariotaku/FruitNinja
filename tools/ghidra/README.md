# tools/ghidra

Committed Ghidra scripts for recurring RE tasks against FruitNinja.exe.

Run these via GhidraMCP (`run_ghidra_script` / `run_script_inline`) with
FruitNinja.exe open in Ghidra. Per-session one-off probes still go in
`tmp/ghidra_scripts/` (gitignored) as usual.

## Scripts

### `AnnotatedDisasm.java`

Print per-instruction annotated disassembly for the function at the current
cursor position. Branch/call targets are annotated with the resolved function
name where available.

```python
# Position cursor at any instruction in the target function, then:
mcp__GhidraMCP__run_ghidra_script(script_name="AnnotatedDisasm")
# or inline:
mcp__GhidraMCP__run_script_inline(script_source=open("tools/ghidra/AnnotatedDisasm.java").read())
```

### `FN_FindUndefRetInRange.java`

Find non-thunk functions with undefined return types in a configurable address
range. Skips std / __gnu_cxx namespaces, ctors, and dtors. Parameterized via
script args (hex strings, no 0x prefix).

```python
# Scan 0x158000-0x180000:
mcp__GhidraMCP__run_ghidra_script(script_name="FN_FindUndefRetInRange",
                                    script_args=["158000", "180000"])
# Scan 0x180000-0x1b9220 (ctor/dtor-filtered):
mcp__GhidraMCP__run_ghidra_script(script_name="FN_FindUndefRetInRange",
                                    script_args=["180000", "1b9220"])
# Scan everything (no range filter):
mcp__GhidraMCP__run_ghidra_script(script_name="FN_FindUndefRetInRange")
```

Output format after `---LIST---`: `<ep_hex>|<type>|<name>|<namespace>` one per line.

### `FN_FixStdContainerSizes.java`

Resize Ghidra placeholder structs for std:: containers and `Mortar::SmartPtr<T>`
to binary-verified sizes:

| Type | Size | Evidence |
|------|------|---------|
| `std::list<T>` | 8 B | Sourcery 2010q1 pre-C++11 (sentinel only) |
| `std::map<K,V>` | 24 B | PowerUpManager 0x18-byte gaps |
| `std::vector<T>` | 12 B | Mesh field offsets |
| `std::basic_string` | 4 B | Sourcery 2010q1 COW single-ptr |
| `Mortar::SmartPtr<T>` | 4 B | Mesh m_OwnGroup offset |

```python
# Dry run first:
mcp__GhidraMCP__run_ghidra_script(script_name="FN_FixStdContainerSizes",
                                    script_args=["DRY"])
# Apply:
mcp__GhidraMCP__run_ghidra_script(script_name="FN_FixStdContainerSizes")
```

Re-run any time Ghidra's type DB drifts (e.g. after importing new type info).

### `ResolveGotStrings.java`

Reference implementation for the GOT-relative string-pointer resolution
pattern used throughout FruitNinja.exe. The hardcoded slots are specific to
`Fruit::LoadInfo` attribute name strings (v1.6.1).

Use this as a **template** for similar GOT-relative string tables in other
functions: copy, update `gotBaseLit` / `r4Offset`, and replace the `slots`
table. See the file header for the resolution algorithm.

```python
mcp__GhidraMCP__run_ghidra_script(script_name="ResolveGotStrings")
```
