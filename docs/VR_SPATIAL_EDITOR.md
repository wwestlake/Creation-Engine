# VR Spatial Editor Contract

## Embodied editor modes

The spatial editor has two user modes, determined from floor-relative headset
height with smoothing and hysteresis. A calibration/override remains available
because headset height is informative, not infallible.

| Physical posture | Avatar/editor state | Primary locomotion |
| --- | --- | --- |
| Sitting | Avatar sits in the editor cart. | Flying cart. |
| Standing | Avatar exits the cart. | Grounded game walking. |

The headset pose supplies the user’s actual height. The cart is a separate
world object; it must never add a second artificial head-height offset.

## Cart lifecycle

- **Flying:** cart moves freely with seated editor controls.
- **Landing:** cart finds valid ground below, descends smoothly, and settles level.
- **Parked:** user may stand, leave the cart, and walk in the game world.
- **Called:** the cart travels to a safe nearby ground position, then waits for
  the user to enter and sit.

No lifecycle transition may snap the camera or teleport the user.

## Spatial editor tray and panels

A small body-relative tray provides icon toggles. Panels are independent:

- **Follow me:** seated panels sit low like a lap console; standing panels rise
  to waist/chest height.
- **Pinned:** panel stays at its authored world position.
- **Collapsed:** panel reduces to a floating icon and reopens in place.

The initial tray icons are Help/Controls, Global Tools, Selected Object,
Keyboard, and Assets. Clicking an icon toggles its own panel.

## Transform and selection

- A ray tests the nearest real object surface and marks that point with a
  small surface-aligned ring.
- Trigger selects that exact intersected object.
- Transform widgets are X-ray editor overlays: always visible through the
  selected object.
- Move supports world and local coordinate space; Rotate and Scale use local
  axes by default.
- Axis arrows constrain one dimension; plane squares constrain any two; center
  control moves all three.

## Global tools

The Global panel owns cart controls, flight speed, World/Local space, snap
rules (grid, surface, vertex, angle and scale), transform mode, and primitive
creation. Primitive creation places a real scene asset with the same transform
tool used for all other objects.
