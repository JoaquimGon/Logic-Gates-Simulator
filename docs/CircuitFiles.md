# Circuit files

In workspace views, File -> Save, Save As, and Open operate on **Main**.
Save remembers its last successful path; Save As always chooses one. Open
validates/builds a separate scene before restoring Main and switching to that tab.
Other authored scenes are retained; compatible embedded definition updates also
reach their placed subcircuits. File -> Load subcircuit imports standalone designs.
In subcircuit editors, the menu instead saves the active subcircuit; each editor
remembers its own path. See [Subcircuits.md](Subcircuits.md) for draft publication,
interface changes, nested dependencies, and editing placed components.
There is no unsaved-change prompt yet. Camera position/zoom remain editor state.

The Windows application uses native JSON file pickers. Cancelling a picker has
no effect. The bar reports success or errors; the console retains error details.
Native pickers currently support Windows; the Persistence API is independent of
GLFW/OpenGL and uses portable filesystem paths, with Windows replacement support.
Time spent in a picker does not advance simulation clocks.

## Stored data

`Persistence/CircuitFile` writes readable UTF-8 JSON with format
`logic-gates-circuit` and version `1`. An empty circuit is:

```json
{
  "format": "logic-gates-circuit",
  "version": 1,
  "name": "Main",
  "definitions": [],
  "components": [],
  "wires": []
}
```

Each component contains `definition`, its exact `version`, grid `position`,
body `label`, and `layout`. Layout stores `[width, height]` in world units and
each pin's stable `id`, `label`, `direction`, directional `index`, relative grid
`anchor`, and optional orthogonal `lead` path (an empty array means automatic).
This preserves edited arity, sizes, and contact geometry without rounding.

Paired native gates also store `inverted`. Manual inputs store `input_state`;
clocks store `clock_frequency` and `clock_paused`. Each wire is an array of
absolute grid `[x, y]` points, retaining bends and disconnected routes.

Native definitions come from the catalog and are referenced by ID/version.
Used custom definitions are embedded, including native-behavior boxes and
subcircuits. Subcircuit definitions carry complete editable designs and ordered
port mappings; nested dependencies are embedded with their saved versions.
Metadata retains layouts/defaults, labels, resize rules, and tint. Shader paths
come from the application's registered presentations rather than JSON.

## Rebuilding and failure behavior

Loading registers embedded definitions, creates components/wires in a batch,
and applies labels/inversion using existing explicit editor operations. It
rebuilds nets and indexed connections from geometry, then propagates signals.
Component/wire/net IDs, simulation caches, hover/selection, and move previews
are not saved. Saving during a preview reads committed placement.

This is a **design save**, not a paused simulation snapshot: clock output/phase
and latch memory start from their native defaults; manual input values are
retained. Propagation computes resulting outputs. Feedback and circuits that
cannot settle remain editable and receive the existing simulation status.

Malformed JSON, unsupported format/definition versions, invalid settings/pins,
and invalid routes fail before Main is replaced. Loading retains valid saved
overlaps rather than relocating components. Limits are 16 MiB per file, 4096
components, 16384 routes, and 128 levels of JSON nesting. Saving checks the same
size/count limits. A complete temporary sibling is written before replacing
the destination; failed writes/replacements preserve the previous save and
remove the temporary file.

## Verification

`circuit_json_roundtrip`, `circuit_json_validation`, and `circuit_file_io` cover
all native types, edited gate arity/inversion, labels, source/clock settings,
custom boxes/leads, independent latch outputs, feedback, runtime reset, preview
exclusion, invalid data, Unicode paths, replacement, and failed-write cleanup.
`circuit_views` checks one-shot File commands and loading Main without replacing
another viewpoint. Subcircuit-specific checks are described in [Subcircuits.md](Subcircuits.md).
Native picker interaction requires an application smoke test.

```sh
ctest --test-dir out/build/x64-debug -R "circuit_json|circuit_file|circuit_views" --output-on-failure
```
