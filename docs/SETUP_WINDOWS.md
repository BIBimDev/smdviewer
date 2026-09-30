# Windows setup - STRAKON SMD Viewer V1

## 1. Install prerequisites

Required:

- Node.js 22 LTS or a newer supported LTS release
- npm (installed together with Node.js)
- a modern browser such as Edge, Chrome, or Firefox

Recommended but optional:

- Visual Studio Code

After installing Node.js, close and reopen PowerShell and check:

```powershell
node --version
npm --version
```

Both commands must print a version.

## 2. Extract the project

Extract `strakon-smd-viewer-v1.zip`, for example to:

```text
C:\StrakonTools\strakon-smd-viewer-v1
```

Open PowerShell in that folder.

Example:

```powershell
cd C:\StrakonTools\strakon-smd-viewer-v1
```

## 3. Install JavaScript packages

Run once:

```powershell
npm install
```

This creates the `node_modules` folder.

The main packages are:

- Three.js - 3D rendering
- Earcut - triangulation of SMD polygon faces
- TypeScript - source language
- Vite - local development/build server

## 4. Start the viewer

```powershell
npm run dev
```

Vite prints a local URL, normally:

```text
http://localhost:5173/
```

Open that URL in a browser.

You can also double-click `start-viewer.cmd`. On the first run it performs `npm install` if `node_modules` does not exist, then starts Vite.

## 5. First validation test

Click **Plain sample**.

Expected:

```text
Name: Stütze (3)
Teilart: 19
Profile: 750 x 650 mm
Vertices: 8
Faces: 6
Bounds: X -375..375 | Y -325..325 | Z -1000..5000 mm
```

The 3D window should show a plain rectangular column.

Then click **Pocket sample**.

Expected:

```text
Name: Stütze (2)
Teilart: 19
Vertices: 16
Faces: 10
Bounds: X -375..375 | Y -325..325 | Z -1000..5000 mm
```

The 3D window should show the top-side pocket/notch.

## 6. Open your own SMD

Use **Open .smd** or drag an `.smd` file onto the 3D view.

Viewer V1 currently expects:

- `"DICAD ASC"`
- a `#10125` main geometry block
- an explicit BREP section containing vertices, faces, planes, and face names

It intentionally does not interpret reinforcement, BIP, special mesh, or other STRAKON blocks yet.

## 7. Controls

- Left mouse: orbit
- Mouse wheel: zoom
- Right mouse: pan
- Fit model: recenter and fit the element

The axes are shown using the Three.js convention colors:

- X = red
- Y = green
- Z = blue

The viewer keeps STRAKON Z as the vertical axis.

## 8. Create a production build

```powershell
npm run build
```

Output:

```text
dist\
```

Test the production build locally with:

```powershell
npm run preview
```

## 9. If PowerShell blocks npm.ps1

If Windows reports that script execution is disabled, you can use the command executable directly:

```powershell
npm.cmd install
npm.cmd run dev
```

This avoids changing the machine-wide PowerShell execution policy.
