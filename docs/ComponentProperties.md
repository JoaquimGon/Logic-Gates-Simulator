# Component properties and instance overrides

## Defaults, configuration, and runtime

`ComponentDefinition` owns immutable reusable defaults. `propertyDescriptors()`
derives types, defaults, limits, choices, and editability from that declaration;
there is no second table of native defaults. Optional `propertyRules` can narrow
bounds, provide labeled choices, or make supported properties read-only. They
cannot introduce unimplemented properties or weaken native constraints.

Every view retains a `ComponentConfiguration`: explicit scalar overrides and an
optional pin layout. An absent override inherits the definition; an explicit
value equal to the default still remains an override. Cloning, move previews,
and reversible scene images preserve this distinction.

Live input signals, clock phase/pause, and latch state belong to simulation.
Toggling an input or pausing a clock does not rewrite its configured initial state.
Property edits apply initial-state or clock settings to the running component
only when those fields are set/reset. Unrelated edits preserve runtime state.

## Reading properties for an inspector

Use the instance's definition identity to look up its catalog declaration:

```cpp
const auto* view = scene.getCommittedComponentView(componentId);
const auto* definition =
    scene.getComponentCatalog().find(view->getDefinitionIdentity().id);
const auto descriptors = propertyDescriptors(*definition);
const auto resolved = resolveConfiguration(*definition, view->getConfiguration());
// resolved.properties: effective values; configuration.overrides: explicit values.
```

| Property ID | Applies to | Type / constraints |
| --- | --- | --- |
| `gate.inputCount` | Gates | Integer; NOT/fixed interfaces read-only; expandable gates 2–255 |
| `input.initialState` | Manual inputs | Boolean |
| `clock.frequencyHz` | Clocks | Finite float, at least 0.1 Hz |
| `clock.initialPaused` | Clocks | Boolean |
| `body.width`, `body.height` | All components | Positive finite floats; read-only when resizing is disabled |
| `presentation.bodyLabel` | All components | String, at most 1024 bytes |
| `presentation.showPinLabels` | All components | Boolean |

`PropertyValue` is strictly `bool`, `int`, `float`, or `std::string`; use `2.0f`
for numeric properties, not an integer or double. Layout compatibility, placement,
and derived body dimensions are also validated. A descriptor's default is the
declaration value; generated gate height may grow with an overridden input count.

## Creating, editing, and resetting

Creation accepts `ComponentOverrides::properties`. Existing typed options remain
adapters to the same validation; supplying a scalar through both its typed option
and the generic property map is rejected. Use the shared action for partial edits:

```cpp
ComponentPropertyPatch patch;
patch.values[ComponentPropertyIds::InputCount] = 4;
auto result = EditorActions(scene).apply({ConfigureProperties{componentId, patch}});

ComponentPropertyPatch reset;
reset.reset = {ComponentPropertyIds::InputCount};
auto resetResult = EditorActions(scene).apply({ConfigureProperties{componentId, reset}});
```

Reset removes the override. Unknown/read-only fields, wrong types, invalid choices,
and conflicting set/reset requests reject the batch without partial changes.
Geometry edits rebuild connectivity once; source settings, labels, and override
provenance alone rebuild none. Legacy configuration actions use this same path.

Generated pins preserve surviving directional indices and stable IDs. An explicit
pin-layout override retains anchors/leads separately from scalar body dimensions.
Changing arity must provide compatible replacement pins or explicitly set
`resetPinLayout = true` to regenerate them. Removed attached pins still require
`RejectAttached`/`LeaveWires` policy; routes are not automatically rerouted.

Changing an instance never changes catalog defaults. Register a separate
definition ID for a reusable variant; the catalog currently stores one immutable
version per ID. Definition migration remains separate work.

## Portable configuration records

`ConfigurationCodec` round-trips design overrides, optional indexed pins/leads,
and exact definition ID/version through a deterministic version-1 typed text
format. Capture and recreate configuration as follows:

```cpp
ComponentConfigurationRecord record{view->getDefinitionIdentity(), view->getConfiguration()};
auto text = encodeConfiguration(scene.getComponentCatalog(), record);
auto decoded = decodeConfiguration(scene.getComponentCatalog(), text);
auto result = EditorActions(scene).apply({CreateComponent{
    decoded.definition.id, {20, 0}, configurationOverrides(decoded.configuration),
    PlacementPolicy::RejectOverlap, decoded.definition.version
}});
```

The format starts with `logic-component-config 1`, followed by `definition`,
`properties`, `pins`, and `end` records. Property entries contain a quoted ID,
`bool`/`int`/`number`/`text` tag, and value. Strings escape quotes/backslashes;
floats retain their exact model precision. Parsing rejects unsupported versions,
duplicate fields, malformed/non-finite values, incompatible definitions/layouts,
and trailing content before any scene mutation. Records are bounded to 8 MiB,
64 property entries, 256 pins, and 256 points per lead.

These APIs do not write files or save a circuit. Definition import/export (RM-C5),
whole-circuit persistence (RM-F1), and inspector widgets (RM-U3) remain pending.

`PropertyTests` covers schema/default consistency, rules/read-only fields, atomic
set/reset and pin migration, runtime isolation/clock phase, snapshot restoration,
and exact configuration round trips plus malformed-record rejection.
