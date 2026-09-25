Check installation in PowerShell:

```powershell
node --version
npm --version
```

## Run

Open PowerShell in this project folder:

```powershell
npm install
npm run dev
```

Vite prints a local address, normally:

```text
http://localhost:5173/
```

Open it in Chrome, Edge, or Firefox.

You can:

- click **Open .smd** and select a local SMD;
- drag an `.smd` onto the 3D area;
- load the included plain-column sample;
- load the included pocket-column sample;
- orbit with left mouse;
- zoom with the mouse wheel;
- pan with right mouse;
- click **Fit model** to recenter the model.

## Build a distributable web folder

```powershell
npm run build
```

The result is written to:

```text
dist\
```

To test the production build locally:

```powershell
npm run preview
```

## Current parser strategy

The viewer does not depend on fixed line numbers. It:

1. verifies `"DICAD ASC"`;
2. finds `#10125`;
3. reads the known profile edge pattern;
4. searches for a self-consistent explicit BREP:
   - `N` vertices followed by `3*N` numeric coordinates;
   - `M` variable-size polygon face records;
   - `M` plane equations (`A B C D`);
   - `M` face-name records;
5. validates every face vertex against its plane equation;
6. detects the confirmed plain-column 3x4 BREP transform when present;
7. triangulates polygon faces with Earcut;
8. renders with Three.js.

## Acceptance samples

### Plain column

Expected:

- profile: 750 x 650 mm
- explicit vertices: 8
- faces: 6
- final bounding box: X `-375..375`, Y `-325..325`, Z `-1000..5000`

### Column with top-side pocket

Expected:

- explicit vertices: 16
- faces: 10
- final bounding box: X `-375..375`, Y `-325..325`, Z `-1000..5000`
- pocket boundary evidence: X `75..375`, Y `-125..75`, Z `4500..5000`

## Important limitation

This is a reverse-engineered Viewer V1, not an official STRAKON/DICAD format implementation. Unsupported geometry should produce an error rather than be guessed.
=======
# smdviewer
>>>>>>> origin/main
