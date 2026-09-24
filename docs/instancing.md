# Shared immutable meshes

Define `JET_MESH_INSTANCING=1` in the application's `JetConfig.hpp` to enable
shared geometry. The default is zero: existing applications retain their Object
layout and renderer path. Use the same configuration throughout the application
and rebuild all translation units when changing it.

Instancing primarily saves memory. Each visible placement still needs its own
transforms, clipping, lighting and rasterization. There is additional transform
and material-selection overhead, which cache reuse can sometimes offset. Measure
the complete scene on its target; a smaller PSRAM footprint does not guarantee
less traffic or faster rendering.

## Author once, place repeatedly

```cpp
// Include Object.hpp and Primitives.hpp; keep paint and both Objects alive.
using namespace Renderer;
Material paint(0xf800);
std::unique_ptr<Object> authored(Primitives::createCube(100, 100, 100, &paint));
auto mesh = Object::freezeMesh(std::move(*authored));
authored.reset();

Object first, second;
first.addInstance(mesh, {});
second.addInstance(mesh, Object::InstanceTransform::rotated({0, 45, 0}));
first.calculateBoundingBox();
second.calculateBoundingBox();
first.setPosition(-100, 0, 500);
second.setPosition(100, 0, 500);
scene.addObject(&first);
scene.addObject(&second);
```

`freezeMesh(Object&&)` prepares bounds and packed positions and moves the geometry
into a `shared_ptr<const Object>`. It rejects nested instances by returning an
empty pointer without consuming the input. `addInstance()` returns false for an
invalid prototype or a per-triangle material table with the wrong length.

An owner can contain its own unique geometry and multiple instances. Rendering
visits the unique mesh first, then instances in insertion order. Placements
inherit the owner's transform, culling, blend, depth, fade and LOD behaviour.
Frustum culling and distance/LOD selection operate on the owner's complete bounds.
Use separate owners when independent visibility or movement matters.

`InstanceTransform` maps prototype-local positions into owner-local space. Its
row-major basis acts on column vectors. Supply a rigid rotation/reflection;
arbitrary scaling and shearing are unsupported. `rotated(degrees, position)`
constructs a rotation and translation. Bake scale before freezing, or use
`bakeScale()` on a batch to create scaled prototypes while retaining sharing
within that batch. `bakeRotation()` preserves instance placements.

Call `calculateBoundingBox()` after adding instances or changing local
placements. Ordinary owner `position`/`rotation` animation requires no mesh
rebuild. Frozen vertex positions, normals, UVs and topology must remain immutable.

## Materials, lifetime and statistics

The optional `Material*` argument to `addInstance()` replaces every material on
that placement. `Object::TriangleMaterials` holds an immutable vector of
`Material*`, indexed by original prototype triangle index. A null table entry
retains the source material and baked colour. A uniform override takes precedence;
any replacement also overrides a triangle's baked colour.

Prototype and table storage are reference counted. Material and texture pointers
remain borrowed, as for ordinary triangles. Keep them alive until their last
render/scanout consumer has finished. Copying an owner shares its prototypes and
tables, while copying the placement records so their transforms can change
independently. Removing the last reference releases the prototype.

`Object::vertexCount()` and `triangleCount()` include all placements. Direct
`vertices.size()` and `triangles.size()` report only an owner's unique mesh.
`Scene::getStatistics()` includes instances when the capability is enabled.

With `MAX_PICK_QUERIES>0`, `PickResult::object` identifies the submitted owner,
`mesh` identifies the actual prototype or selected LOD, and `instanceIndex`
identifies its placement (`-1` for ordinary geometry). `triangleIndex` refers to
the original triangle in `mesh`, even with `SORT_TRIANGLES=1`; sorting uses a
temporary index array without mutating the shared topology. Results borrow their
pointers and remain usable only while the associated objects/meshes live.
