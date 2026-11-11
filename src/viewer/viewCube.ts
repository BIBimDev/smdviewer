import * as THREE from "three";

export interface ViewCubeHandle {
  setVisible(visible: boolean): void;
  syncFromCamera(camera: THREE.Camera): void;
  dispose(): void;
}

interface ViewCubeOptions {
  onSnap(direction: THREE.Vector3): void;
  onOrbit(deltaX: number, deltaY: number): void;
}

interface GridCoordinate {
  x: number;
  y: number;
  z: number;
}

type Cubelet = THREE.Mesh<THREE.BoxGeometry, THREE.MeshBasicMaterial[]>;

interface CubeTileHit {
  intersection: THREE.Intersection;
  cubelet: Cubelet;
  materialIndex: number;
  grid: GridCoordinate;
}

/*
 * Slightly smaller control than the previous 176 px version.
 * The orthographic mini-camera below provides the extra visual padding,
 * so reducing the CSS/control size does NOT crop the cube at corners.
 */
const VIEW_CUBE_SIZE = 164;
const MINI_CAMERA_DISTANCE = 6.5;
const ORTHO_HALF_SIZE = 2.35;

const CUBELET_SIZE = 0.61;
const CUBELET_STEP = 0.69;

const ACTIVE_TILE_COLOR = 0xf59e0b;
const HOVER_TILE_COLOR = 0xfb923c;
const EDGE_COLOR = 0x334155;

const X_AXIS_COLOR = 0xef4444;
const Y_AXIS_COLOR = 0x22c55e;
const Z_AXIS_COLOR = 0x3b82f6;

/*
 * THREE.BoxGeometry material order:
 *
 * 0 = +X = RIGHT
 * 1 = -X = LEFT
 * 2 = +Y = BACK
 * 3 = -Y = FRONT
 * 4 = +Z = TOP
 * 5 = -Z = BOTTOM
 */
const FACE_COLORS = [
  0x9fc3e8,
  0x86add8,
  0x91b7df,
  0xa8c9eb,
  0xc3daf1,
  0x789bc6,
];

const GRID_VALUES = [-1, 0, 1] as const;

const CANONICAL_DIRECTIONS: GridCoordinate[] = [];
for (const x of GRID_VALUES) {
  for (const y of GRID_VALUES) {
    for (const z of GRID_VALUES) {
      if (x === 0 && y === 0 && z === 0) {
        continue;
      }
      CANONICAL_DIRECTIONS.push({ x, y, z });
    }
  }
}

function gridKey(grid: GridCoordinate): string {
  return `${grid.x},${grid.y},${grid.z}`;
}

function sameGrid(a: GridCoordinate | null, b: GridCoordinate): boolean {
  return !!a && a.x === b.x && a.y === b.y && a.z === b.z;
}

function normalizedAxis(normal: THREE.Vector3): THREE.Vector3 {
  const absX = Math.abs(normal.x);
  const absY = Math.abs(normal.y);
  const absZ = Math.abs(normal.z);

  if (absX >= absY && absX >= absZ) {
    return new THREE.Vector3(Math.sign(normal.x) || 1, 0, 0);
  }

  if (absY >= absX && absY >= absZ) {
    return new THREE.Vector3(0, Math.sign(normal.y) || 1, 0);
  }

  return new THREE.Vector3(0, 0, Math.sign(normal.z) || 1);
}

function faceIndexFromAxis(axis: THREE.Vector3): number {
  if (axis.x > 0.5) {
    return 0;
  }
  if (axis.x < -0.5) {
    return 1;
  }
  if (axis.y > 0.5) {
    return 2;
  }
  if (axis.y < -0.5) {
    return 3;
  }
  if (axis.z > 0.5) {
    return 4;
  }
  return 5;
}

function isOuterFace(grid: GridCoordinate, axis: THREE.Vector3): boolean {
  if (axis.x > 0.5) {
    return grid.x === 1;
  }
  if (axis.x < -0.5) {
    return grid.x === -1;
  }
  if (axis.y > 0.5) {
    return grid.y === 1;
  }
  if (axis.y < -0.5) {
    return grid.y === -1;
  }
  if (axis.z > 0.5) {
    return grid.z === 1;
  }
  return grid.z === -1;
}

function isOutwardMaterialFace(
  grid: GridCoordinate,
  materialIndex: number,
): boolean {
  switch (materialIndex) {
    case 0:
      return grid.x === 1;
    case 1:
      return grid.x === -1;
    case 2:
      return grid.y === 1;
    case 3:
      return grid.y === -1;
    case 4:
      return grid.z === 1;
    case 5:
      return grid.z === -1;
    default:
      return false;
  }
}

function directionFromTile(
  grid: GridCoordinate,
  faceAxis: THREE.Vector3,
): THREE.Vector3 {
  return new THREE.Vector3(
    Math.abs(faceAxis.x) > 0.5 ? faceAxis.x : grid.x,
    Math.abs(faceAxis.y) > 0.5 ? faceAxis.y : grid.y,
    Math.abs(faceAxis.z) > 0.5 ? faceAxis.z : grid.z,
  ).normalize();
}

function nearestCanonicalGrid(direction: THREE.Vector3): GridCoordinate {
  const normalized = direction.clone().normalize();

  let best = CANONICAL_DIRECTIONS[0];
  let bestDot = -Infinity;

  for (const candidate of CANONICAL_DIRECTIONS) {
    const vector = new THREE.Vector3(
      candidate.x,
      candidate.y,
      candidate.z,
    ).normalize();
    const dot = normalized.dot(vector);

    if (dot > bestDot) {
      bestDot = dot;
      best = candidate;
    }
  }

  return best;
}

function directionLabel(direction: THREE.Vector3): string {
  const grid = nearestCanonicalGrid(direction);
  const parts: string[] = [];

  if (grid.z > 0) {
    parts.push("Top");
  } else if (grid.z < 0) {
    parts.push("Bottom");
  }

  if (grid.y < 0) {
    parts.push("Front");
  } else if (grid.y > 0) {
    parts.push("Back");
  }

  if (grid.x < 0) {
    parts.push("Left");
  } else if (grid.x > 0) {
    parts.push("Right");
  }

  if (parts.length === 1) {
    return `${parts[0]} view`;
  }

  if (parts.length === 2) {
    return `${parts.join(" + ")} view`;
  }

  return `${parts.join(" + ")} isometric view`;
}

function createAxisLabelSprite(
  text: string,
  color: number,
): THREE.Sprite {
  const canvas = document.createElement("canvas");
  canvas.width = 128;
  canvas.height = 128;

  const context = canvas.getContext("2d");
  if (!context) {
    throw new Error("Could not create view-cube axis label canvas.");
  }

  context.clearRect(0, 0, canvas.width, canvas.height);
  context.font = "700 62px Arial, sans-serif";
  context.textAlign = "center";
  context.textBaseline = "middle";
  context.lineWidth = 8;
  context.strokeStyle = "rgba(255,255,255,0.95)";
  context.strokeText(text, 64, 67);
  context.fillStyle = `#${color.toString(16).padStart(6, "0")}`;
  context.fillText(text, 64, 67);

  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;

  const material = new THREE.SpriteMaterial({
    map: texture,
    transparent: true,
    depthTest: false,
    depthWrite: false,
  });

  const sprite = new THREE.Sprite(material);
  sprite.scale.set(0.42, 0.42, 1);
  sprite.renderOrder = 20;
  sprite.userData.axisTexture = texture;
  return sprite;
}

function createAxisLine(
  start: THREE.Vector3,
  end: THREE.Vector3,
  color: number,
): THREE.Line {
  const geometry = new THREE.BufferGeometry().setFromPoints([start, end]);
  const material = new THREE.LineBasicMaterial({
    color,
    depthTest: false,
    depthWrite: false,
  });
  const line = new THREE.Line(geometry, material);
  line.renderOrder = 19;
  return line;
}

export function createViewCube(
  container: HTMLElement,
  options: ViewCubeOptions,
): ViewCubeHandle {
  const host = document.createElement("div");
  host.className = "view-cube";
  host.setAttribute("aria-label", "Rubik-style interactive view cube");
  container.appendChild(host);

  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setSize(VIEW_CUBE_SIZE, VIEW_CUBE_SIZE, false);
  renderer.setClearColor(0x000000, 0);
  renderer.domElement.className = "view-cube-canvas";
  renderer.domElement.tabIndex = 0;
  renderer.domElement.setAttribute(
    "aria-label",
    "Rubik-style view cube. Click a center tile for a face view, an edge tile for a two-axis view, or a corner tile for an isometric view. Drag to orbit.",
  );
  host.appendChild(renderer.domElement);

  const scene = new THREE.Scene();

  /*
   * Orthographic camera = CAD-style navigation cube.
   *
   * Unlike the previous perspective mini-camera, the orthographic frustum
   * keeps the entire 3 x 3 cube visible at face, edge and corner rotations.
   * Extra padding also leaves room for the X/Y/Z labels.
   */
  const camera = new THREE.OrthographicCamera(
    -ORTHO_HALF_SIZE,
    ORTHO_HALF_SIZE,
    ORTHO_HALF_SIZE,
    -ORTHO_HALF_SIZE,
    0.1,
    30,
  );
  camera.position.set(3, -3, 3).normalize().multiplyScalar(MINI_CAMERA_DISTANCE);
  camera.up.set(0, 0, 1);
  camera.lookAt(0, 0, 0);
  camera.updateProjectionMatrix();

  /*
   * Build a true 3 x 3 x 3 Rubik-style cube. The hidden center cubelet is
   * omitted, leaving 26 surface cubelets. Each visible side therefore contains
   * nine real tiles.
   */
  const cubeRoot = new THREE.Group();
  scene.add(cubeRoot);

  const cubeletGeometry = new THREE.BoxGeometry(
    CUBELET_SIZE,
    CUBELET_SIZE,
    CUBELET_SIZE,
  );
  const cubeletEdgeGeometry = new THREE.EdgesGeometry(cubeletGeometry);
  const cubeletEdgeMaterial = new THREE.LineBasicMaterial({ color: EDGE_COLOR });

  const cubelets: Cubelet[] = [];
  const cubeletByKey = new Map<string, Cubelet>();

  for (const x of GRID_VALUES) {
    for (const y of GRID_VALUES) {
      for (const z of GRID_VALUES) {
        if (x === 0 && y === 0 && z === 0) {
          continue;
        }

        const materials = FACE_COLORS.map(
          (color) =>
            new THREE.MeshBasicMaterial({
              color,
            }),
        );

        const cubelet: Cubelet = new THREE.Mesh(cubeletGeometry, materials);
        cubelet.position.set(
          x * CUBELET_STEP,
          y * CUBELET_STEP,
          z * CUBELET_STEP,
        );
        cubelet.userData.grid = { x, y, z } satisfies GridCoordinate;
        cubeRoot.add(cubelet);
        cubelets.push(cubelet);
        cubeletByKey.set(gridKey({ x, y, z }), cubelet);

        const edges = new THREE.LineSegments(
          cubeletEdgeGeometry,
          cubeletEdgeMaterial,
        );
        edges.position.copy(cubelet.position);
        edges.renderOrder = 3;
        cubeRoot.add(edges);
      }
    }
  }

  /*
   * Positive-axis indicators. They sit just outside the cube and are rendered
   * without depth testing so the navigation orientation stays readable.
   */
  const axisRoot = new THREE.Group();
  scene.add(axisRoot);

  const axisStart = 1.05;
  const axisEnd = 1.55;
  const labelDistance = 1.78;

  const xLine = createAxisLine(
    new THREE.Vector3(axisStart, 0, 0),
    new THREE.Vector3(axisEnd, 0, 0),
    X_AXIS_COLOR,
  );
  const yLine = createAxisLine(
    new THREE.Vector3(0, axisStart, 0),
    new THREE.Vector3(0, axisEnd, 0),
    Y_AXIS_COLOR,
  );
  const zLine = createAxisLine(
    new THREE.Vector3(0, 0, axisStart),
    new THREE.Vector3(0, 0, axisEnd),
    Z_AXIS_COLOR,
  );

  axisRoot.add(xLine, yLine, zLine);

  const xLabel = createAxisLabelSprite("X", X_AXIS_COLOR);
  const yLabel = createAxisLabelSprite("Y", Y_AXIS_COLOR);
  const zLabel = createAxisLabelSprite("Z", Z_AXIS_COLOR);

  xLabel.position.set(labelDistance, 0, 0);
  yLabel.position.set(0, labelDistance, 0);
  zLabel.position.set(0, 0, labelDistance);

  axisRoot.add(xLabel, yLabel, zLabel);

  const raycaster = new THREE.Raycaster();
  const pointer = new THREE.Vector2();

  let hoveredTile: CubeTileHit | null = null;
  let activeGrid: GridCoordinate | null = null;

  let pointerDownX = 0;
  let pointerDownY = 0;
  let lastX = 0;
  let lastY = 0;
  let dragging = false;
  let moved = false;

  function renderCube(): void {
    renderer.render(scene, camera);
  }

  function updateCubeletColors(): void {
    for (const cubelet of cubelets) {
      const grid = cubelet.userData.grid as GridCoordinate;
      const isActiveCubelet = sameGrid(activeGrid, grid);

      cubelet.material.forEach((material: THREE.MeshBasicMaterial, materialIndex: number) => {
        let color = FACE_COLORS[materialIndex];

        if (isActiveCubelet && isOutwardMaterialFace(grid, materialIndex)) {
          color = ACTIVE_TILE_COLOR;
        }

        if (
          hoveredTile &&
          hoveredTile.cubelet === cubelet &&
          hoveredTile.materialIndex === materialIndex
        ) {
          color = HOVER_TILE_COLOR;
        }

        material.color.setHex(color);
      });
    }
  }

  function setHoveredTile(hit: CubeTileHit | null): void {
    const same =
      hoveredTile?.cubelet === hit?.cubelet &&
      hoveredTile?.materialIndex === hit?.materialIndex;

    if (same) {
      return;
    }

    hoveredTile = hit;
    updateCubeletColors();
    renderCube();
  }

  function updatePointer(event: PointerEvent): void {
    const rect = renderer.domElement.getBoundingClientRect();
    pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
  }

  function hoveredCubeTile(event: PointerEvent): CubeTileHit | null {
    updatePointer(event);
    raycaster.setFromCamera(pointer, camera);

    const intersections = raycaster.intersectObjects(cubelets, false);

    for (const intersection of intersections) {
      if (!intersection.face) {
        continue;
      }

      const cubelet = intersection.object as Cubelet;
      const grid = cubelet.userData.grid as GridCoordinate;
      const faceAxis = normalizedAxis(intersection.face.normal);

      /* Ignore internal cubelet side faces exposed by the gaps. */
      if (!isOuterFace(grid, faceAxis)) {
        continue;
      }

      return {
        intersection,
        cubelet,
        materialIndex: faceIndexFromAxis(faceAxis),
        grid,
      };
    }

    return null;
  }

  function handlePointerMove(event: PointerEvent): void {
    if (dragging) {
      const dx = event.clientX - lastX;
      const dy = event.clientY - lastY;
      lastX = event.clientX;
      lastY = event.clientY;

      if (
        Math.hypot(
          event.clientX - pointerDownX,
          event.clientY - pointerDownY,
        ) > 3
      ) {
        moved = true;
      }

      if (moved) {
        setHoveredTile(null);
        options.onOrbit(dx, dy);
      }

      renderer.domElement.style.cursor = "grabbing";
      return;
    }

    const hit = hoveredCubeTile(event);
    setHoveredTile(hit);

    if (hit?.intersection.face) {
      const direction = directionFromTile(
        hit.grid,
        normalizedAxis(hit.intersection.face.normal),
      );
      renderer.domElement.title = directionLabel(direction);
      renderer.domElement.style.cursor = "pointer";
    } else {
      renderer.domElement.removeAttribute("title");
      renderer.domElement.style.cursor = "grab";
    }
  }

  function handlePointerDown(event: PointerEvent): void {
    pointerDownX = event.clientX;
    pointerDownY = event.clientY;
    lastX = event.clientX;
    lastY = event.clientY;
    moved = false;
    dragging = true;

    renderer.domElement.setPointerCapture(event.pointerId);
    renderer.domElement.style.cursor = "grabbing";
    event.preventDefault();
  }

  function handlePointerUp(event: PointerEvent): void {
    if (renderer.domElement.hasPointerCapture(event.pointerId)) {
      renderer.domElement.releasePointerCapture(event.pointerId);
    }

    const wasDragging = dragging;
    dragging = false;

    if (wasDragging && !moved) {
      const hit = hoveredCubeTile(event);

      if (hit?.intersection.face) {
        const faceAxis = normalizedAxis(hit.intersection.face.normal);
        const direction = directionFromTile(hit.grid, faceAxis);
        options.onSnap(direction);
      }
    }

    const hoverAfterClick = hoveredCubeTile(event);
    setHoveredTile(hoverAfterClick);
    renderer.domElement.style.cursor = hoverAfterClick ? "pointer" : "grab";
  }

  function handlePointerLeave(): void {
    if (!dragging) {
      setHoveredTile(null);
      renderer.domElement.removeAttribute("title");
      renderer.domElement.style.cursor = "grab";
    }
  }

  renderer.domElement.addEventListener("pointermove", handlePointerMove);
  renderer.domElement.addEventListener("pointerdown", handlePointerDown);
  renderer.domElement.addEventListener("pointerup", handlePointerUp);
  renderer.domElement.addEventListener("pointerleave", handlePointerLeave);

  function syncFromCamera(mainCamera: THREE.Camera): void {
    /*
     * mainCamera.getWorldDirection() points from the real camera toward the
     * model. The navigation-cube camera therefore sits on the opposite side
     * of the origin to present exactly the same visible cube sides.
     */
    const mainForward = new THREE.Vector3();
    mainCamera.getWorldDirection(mainForward).normalize();

    const cameraFromModel = mainForward.clone().multiplyScalar(-1);

    camera.position
      .copy(cameraFromModel)
      .normalize()
      .multiplyScalar(MINI_CAMERA_DISTANCE);

    const mainScreenUp = new THREE.Vector3(0, 1, 0)
      .applyQuaternion(mainCamera.quaternion)
      .normalize();

    /*
     * Avoid lookAt degeneracy if the supplied screen-up vector becomes almost
     * parallel with the viewing direction. Top/bottom views use +Y as a stable
     * fallback; side views use world +Z.
     */
    let stableUp = mainScreenUp;
    if (Math.abs(cameraFromModel.dot(stableUp)) > 0.98) {
      stableUp =
        Math.abs(cameraFromModel.z) > 0.9
          ? new THREE.Vector3(0, 1, 0)
          : new THREE.Vector3(0, 0, 1);
    }

    camera.up.copy(stableUp);
    camera.lookAt(0, 0, 0);
    camera.updateMatrixWorld();

    const nextActiveGrid = nearestCanonicalGrid(cameraFromModel);
    if (!sameGrid(activeGrid, nextActiveGrid)) {
      activeGrid = nextActiveGrid;
      updateCubeletColors();
    }

    renderCube();
  }

  /* Initial isometric orientation. */
  activeGrid = nearestCanonicalGrid(camera.position);
  updateCubeletColors();
  renderCube();

  return {
    setVisible(visible: boolean): void {
      host.classList.toggle("hidden", !visible);
      if (visible) {
        renderCube();
      }
    },

    syncFromCamera,

    dispose(): void {
      renderer.domElement.removeEventListener("pointermove", handlePointerMove);
      renderer.domElement.removeEventListener("pointerdown", handlePointerDown);
      renderer.domElement.removeEventListener("pointerup", handlePointerUp);
      renderer.domElement.removeEventListener("pointerleave", handlePointerLeave);

      for (const cubelet of cubelets) {
        cubelet.material.forEach((material: THREE.MeshBasicMaterial) => material.dispose());
      }

      cubeletGeometry.dispose();
      cubeletEdgeGeometry.dispose();
      cubeletEdgeMaterial.dispose();

      for (const line of [xLine, yLine, zLine]) {
        line.geometry.dispose();
        (line.material as THREE.Material).dispose();
      }

      for (const label of [xLabel, yLabel, zLabel]) {
        const material = label.material as THREE.SpriteMaterial;
        const texture = label.userData.axisTexture as THREE.Texture | undefined;
        texture?.dispose();
        material.dispose();
      }

      renderer.dispose();
      host.remove();
      cubeletByKey.clear();
    },
  };
}
