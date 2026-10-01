# Component definitions and catalog

## Where declarations belong

`src/Components/Definitions/NativeDefinitions.cpp` is the declaration table for
all eleven built-ins. It owns editor defaults, stable type IDs/versions, behavior
references, body sizes, pin identities/labels/anchors, and shader resources.
Body sizes and generated gate layouts use `Geometry/GridMetrics.h` spacing.
Shortcuts map keys to IDs; Engine supplies IDs and positions for its initial scene.
Views receive resolved data and contain no creation presets.

Native behavior implementations remain under `Components/`; Circuit owns their
simulation. Adding a presentation/default variant uses a new declaration. Adding
an entirely new behavior family also requires its implementation, a
`NativeBehavior` alternative, and a factory dispatch branch.

## Definition and instance data

`ComponentDefinition` contains reusable metadata without GLM, GLFW, or OpenGL.
Pin IDs are unique within a definition; directional numeric indices map those
IDs to the current logic interface. Vector order does not determine identity.
Positions and live signal, timer, and latch state belong to instances.

`ComponentOverrides` accepts gate input count, manual input state, clock
frequency/pause, and explicit instance layout. Missing fields use catalog
defaults. Resolution produces a copy; configuration cannot mutate the definition.
Views retain definition ID/version, explicit property overrides, and resolved pin
IDs/labels through previews, configuration, and snapshot restoration. Generic
property descriptors, set/reset edits, and portable design-configuration records
are described in [ComponentProperties.md](ComponentProperties.md).

Fixed interfaces retain their counts and IDs. Multi-input native gates use
`SymmetricGateInputs`: surviving IDs remain `in.<index>`, appended inputs receive
new IDs, and generated rows/body height expand together. Explicit layouts retain
legacy anchor/size customization; `allowResize` restricts body changes. NOT has
one input; other gates require at least two. Definitions currently allow at most
256 total pins. Clock frequency must be finite and at least 0.1 Hz.

Layout edits preserve IDs, labels, and presentation; they cannot silently reassign
pins. Removed attached inputs still require the existing explicit wire policy.
Definition-version migrations and automatic wire rerouting remain separate work.

## Creating and registering

Use the shared editor action for keyboard, startup, palette, and loader adapters:

```cpp
ComponentOverrides options;
options.inputCount = 3;
auto result = EditorActions(scene).apply({
    CreateComponent{BuiltinComponentIds::And, {0, 0}, options}
});
```

`ComponentFactory` resolves/validates before constructing matching logic and views.
EditorActions validates final placement and publishes one topology rebuild.
The older native creation requests/Scene helpers adapt explicit geometry to this
same path. Their supplied shader must match the selected definition; these APIs
no longer supply independent defaults.

`scene.getComponentCatalog()` provides read-only lookup/enumeration. A catalog
stores one version per ID. Creation can request that exact version; version zero
selects the registered version. Unknown IDs/versions produce an edit failure.

Custom definitions use new IDs outside `native.*`, reference a supported native
behavior, and use the shared box presentation. For example:

```cpp
ComponentDefinition definition;
definition.identity = {"user.inverter", 1};
definition.displayName = "Custom inverter";
definition.behavior = NOT;
definition.layout = {0.3f, 0.2f, {
    {"data", "Data", PinType::INPUT, 0, {-3, 0}, {}},
    {"result", "Result", PinType::OUTPUT, 0, {3, 0}, {}}
}};
definition.presentation.shader = boxShaderResources();
definition.presentation.bodyLabel = "INVERTER";
definition.presentation.showPinLabels = true;

auto result = EditorActions(scene).apply({
    RegisterComponentDefinition{definition},
    CreateComponent{"user.inverter", {20, 0}, {}}
});
```

Registration and creation can share an atomic batch. Invalid/duplicate metadata,
unsupported versions/options, bad interfaces, and placement failures leave the
scene and catalog unchanged. Catalog storage is shared read-only across snapshots;
registration copies it once per batch. Definition-only registration rebuilds no
topology. Reacquire borrowed definition/view pointers by ID after committed edits.

## Presentation and remaining work

Declarations provide body contours, tints, shader resources, and label policy.
Custom boxes use the shared rounded-box presentation; copying a native descriptor
into a box requires resetting `presentation.body` to its box defaults. Behavior
still comes from the native reference. Common view metadata becomes typed instance
values; each body has its own dimensions/tint, and no concrete component casts
are used for labels.

Optional leads now accept 2–256 orthogonal relative grid points ending at the
anchor. They are visual geometry and do not create electrical connections.
Automatic stubs bridge native/box silhouettes to existing anchors without moving
pins. Layout/configuration/snapshots retain explicit routes and validate overflow.
See [rendering boundaries](Rendering.md) for shape/resize, label, layer, and GPU
ownership contracts plus framebuffer validation.

Property schemas and retained/serialized instance overrides are complete (RM-C3).
Definition file formats/import/export remain RM-C5; truth tables/subcircuits are
RM-C6. UI capture and viewport transforms are complete; inspector widgets remain
RM-U3, and whole-circuit persistence remains RM-F1.

`ComponentCatalogTests` covers defaults, assets, registration validation,
applicable options, instance/default isolation, working custom behavior, atomic
rollback, and catalog/identity restoration. Input tests verify all eleven shortcuts
against catalog identities and sizes. Presentation tests cover leads, instance
sizes, stable ordering, and text layout; pixel tests verify native attachment and
mixed-size rendering. Property tests cover rules, set/reset, runtime isolation,
pin migration, and configuration round trips. Current validation passes 41 Debug
groups and 40 headless AddressSanitizer groups. Neutral descriptor/catalog headers require no graphics
include paths.
