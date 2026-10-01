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
Views retain definition ID/version and resolved pin IDs/labels through previews,
copies, configuration, and snapshot restoration.

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

Renderer loads native shader resources from the same table and reads body/pin
labels through ComponentView/PinUI. Custom boxes reuse the existing rounded-box
fragment shader under the `box` key; behavior still comes from the native reference.

Nonempty pin leads are rejected until their geometry/rendering contract exists.
Property schemas/serialized override storage remain RM-C3; file formats and
import/export are RM-C5; truth tables/subcircuits are RM-C6. Mixed-size batching
(RM-A6/TD-R1) and native shape/anchor alignment (TD-R3) remain open. This change's
headless tests validate metadata and behavior, rather than pixel appearance.

`ComponentCatalogTests` covers defaults, assets, registration validation,
applicable options, instance/default isolation, working custom behavior, atomic
rollback, and catalog/identity restoration. Input tests verify all eleven shortcuts
against catalog identities and sizes. Debug and headless AddressSanitizer builds
pass all 28 groups; neutral descriptor/catalog headers compile without graphics
include paths.
