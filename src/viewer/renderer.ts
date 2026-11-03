import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { CSS2DObject, CSS2DRenderer } from "three/examples/jsm/renderers/CSS2DRenderer.js";
import type { ParsedSmd, Vec3 } from "../smd/types";
import { triangulateFace } from "./triangulation";

export interface ViewerInteractionHandlers {
  onVertexHover?: (vertexIndex: number | null) => void;
  onVertexSelect?: (vertexIndex: number | null) => void;
  onFaceHover?: (faceIndex: number | null) => void;
  onFaceSelect?: (faceIndex: number | null) => void;
}

export interface ViewerHandle {
  load(parsed: ParsedSmd): void;
  clear(): void;
  fit(): void;
  dispose(): void;
  setVerticesVisible(visible: boolean): void;
  setFaceSelectMode(enabled: boolean): void;
  setFaceColors(enabled: boolean): void;
  setHoveredVertex(vertexIndex: number | null): void;
  setHoveredFace(faceIndex: number | null): void;
  selectVertex(vertexIndex: number | null): void;
  selectFace(faceIndex: number | null): void;
  setInteractionHandlers(handlers: ViewerInteractionHandlers): void;
}

const DEFAULT_FACE_COLOR = 0xc9cdd2;
const HOVER_FACE_COLOR = 0xffd166;
const SELECTED_FACE_COLOR = 0xff8c42;

// Vertex markers are intentionally screen-sized points instead of 3D spheres.
// This keeps dense BREP vertex sets readable at every zoom level.
const DEFAULT_VERTEX_COLOR = 0x64748b;
const HOVER_VERTEX_COLOR = 0xfacc15;
const SELECTED_VERTEX_COLOR = 0xef4444;
const HOVER_FACE_VERTEX_COLOR = 0x0ea5e9;
const SELECTED_FACE_VERTEX_COLOR = 0xf97316;
const MUTED_VERTEX_COLOR = 0xaeb8c4;

const FACE_PALETTE = [
  0x8ecae6,
  0xffb703,
  0x90be6d,
  0xf8961e,
  0xb8c0ff,
  0x43aa8b,
  0xf28482,
  0x84a59d,
  0xcdb4db,
  0x72a1e5,
  0xf6bd60,
  0xa8dadc,
];

function buildGeometry(parsed: ParsedSmd): THREE.BufferGeometry {
  const vertices = parsed.geometry.brep.verticesWorld;
  const positions: number[] = [];

  for (const face of parsed.geometry.brep.faces) {
    const triangles = triangulateFace(vertices, face);

    for (const triangle of triangles) {
      for (const vertexIndex of triangle) {
        const v = vertices[vertexIndex];
        positions.push(v.x, v.y, v.z);
      }
    }
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geometry.computeVertexNormals();
  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();
  return geometry;
}

function buildFaceGeometry(parsed: ParsedSmd, faceIndex: number): THREE.BufferGeometry {
  const vertices = parsed.geometry.brep.verticesWorld;
  const face = parsed.geometry.brep.faces[faceIndex];
  const triangles = triangulateFace(vertices, face);
  const positions: number[] = [];

  for (const triangle of triangles) {
    for (const vertexIndex of triangle) {
      const v = vertices[vertexIndex];
      positions.push(v.x, v.y, v.z);
    }
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  geometry.computeVertexNormals();
  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();
  return geometry;
}

function makeGrid(size: number): THREE.GridHelper {
  const safeSize = Math.max(size, 1000);
  const divisions = 20;
  const grid = new THREE.GridHelper(safeSize, divisions);
  // Three.js GridHelper lies on XZ. STRAKON uses Z as vertical, so rotate to XY.
  grid.rotation.x = Math.PI / 2;
  return grid;
}

function formatCoordinate(value: number): string {
  if (Math.abs(value - Math.round(value)) < 1e-6) {
    return String(Math.round(value));
  }
  return value.toFixed(3);
}

export function createViewer(container: HTMLElement): ViewerHandle {
  THREE.Object3D.DEFAULT_UP.set(0, 0, 1);

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0xf4f6f8);

  const camera = new THREE.PerspectiveCamera(45, 1, 1, 1_000_000);
  camera.up.set(0, 0, 1);

  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.domElement.className = "viewer-canvas";
  container.appendChild(renderer.domElement);

  const labelRenderer = new CSS2DRenderer();
  labelRenderer.domElement.className = "label-layer";
  labelRenderer.domElement.style.position = "absolute";
  labelRenderer.domElement.style.inset = "0";
  labelRenderer.domElement.style.pointerEvents = "none";
  container.appendChild(labelRenderer.domElement);

  const controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;

  const ambient = new THREE.AmbientLight(0xffffff, 1.8);
  scene.add(ambient);

  const keyLight = new THREE.DirectionalLight(0xffffff, 2.3);
  keyLight.position.set(1, -2, 3);
  scene.add(keyLight);

  const fillLight = new THREE.DirectionalLight(0xffffff, 1.2);
  fillLight.position.set(-2, 1, 1);
  scene.add(fillLight);

  const axes = new THREE.AxesHelper(1000);
  scene.add(axes);

  const raycaster = new THREE.Raycaster();
  const pointer = new THREE.Vector2();

  let modelRoot: THREE.Group | undefined;
  let vertexRoot: THREE.Group | undefined;
  let grid: THREE.GridHelper | undefined;
  let currentParsed: ParsedSmd | undefined;
  let edgeGeometry: THREE.EdgesGeometry | undefined;
  let edgeMaterial: THREE.LineBasicMaterial | undefined;
  let vertexPointGeometry: THREE.BufferGeometry | undefined;

  const faceMeshes: THREE.Mesh<THREE.BufferGeometry, THREE.MeshStandardMaterial>[] = [];
  const vertexNodes: THREE.Points<THREE.BufferGeometry, THREE.PointsMaterial>[] = [];
  const vertexLabels: CSS2DObject[] = [];

  let verticesVisible = false;
  let faceSelectMode = false;
  let faceColorsEnabled = false;

  let hoveredVertex: number | null = null;
  let selectedVertex: number | null = null;
  let hoveredFace: number | null = null;
  let selectedFace: number | null = null;

  let hoveredFaceVertices = new Set<number>();
  let selectedFaceVertices = new Set<number>();
  let handlers: ViewerInteractionHandlers = {};

  function faceColor(faceIndex: number): number {
    return FACE_PALETTE[faceIndex % FACE_PALETTE.length];
  }

  function faceVertices(faceIndex: number | null): Set<number> {
    const result = new Set<number>();
    if (faceIndex === null || !currentParsed) {
      return result;
    }
    const face = currentParsed.geometry.brep.faces[faceIndex];
    if (!face) {
      return result;
    }
    for (const index of face.indices) {
      result.add(index);
    }
    return result;
  }

  function fullVertexLabel(vertexIndex: number): string {
    if (!currentParsed) {
      return `V${vertexIndex}`;
    }
    const vertex = currentParsed.geometry.brep.verticesWorld[vertexIndex];
    if (!vertex) {
      return `V${vertexIndex}`;
    }
    return `V${vertexIndex} (${formatCoordinate(vertex.x)}, ${formatCoordinate(vertex.y)}, ${formatCoordinate(vertex.z)})`;
  }

  function updateFaceAppearance(faceIndex: number): void {
    const mesh = faceMeshes[faceIndex];
    if (!mesh) {
      return;
    }

    let color = DEFAULT_FACE_COLOR;
    if (faceColorsEnabled) {
      color = faceColor(faceIndex);
    }
    if (hoveredFace === faceIndex) {
      color = HOVER_FACE_COLOR;
    }
    if (selectedFace === faceIndex) {
      color = SELECTED_FACE_COLOR;
    }

    mesh.material.color.setHex(color);
  }

  function updateAllFaceAppearances(): void {
    faceMeshes.forEach((_, index) => updateFaceAppearance(index));
  }

  function updateVertexAppearance(vertexIndex: number): void {
    const node = vertexNodes[vertexIndex];
    const label = vertexLabels[vertexIndex];
    if (!node || !label) {
      return;
    }

    const isHovered = hoveredVertex === vertexIndex;
    const isSelected = selectedVertex === vertexIndex;
    const isHoveredFaceVertex = hoveredFaceVertices.has(vertexIndex);
    const isSelectedFaceVertex = selectedFaceVertices.has(vertexIndex);
    const isEmphasized = isHovered || isSelected || isHoveredFaceVertex || isSelectedFaceVertex;
    const shouldShow = verticesVisible || isEmphasized;
    const hasVertexSelection = verticesVisible && selectedVertex !== null;
    const isMuted = hasVertexSelection && !isHovered && !isSelected && !isHoveredFaceVertex && !isSelectedFaceVertex;

    node.visible = shouldShow;
    label.visible = shouldShow;

    let color = DEFAULT_VERTEX_COLOR;
    let pointSize = 5.5;

    if (isMuted) {
      color = MUTED_VERTEX_COLOR;
      pointSize = 4;
    }
    if (isHoveredFaceVertex) {
      color = HOVER_FACE_VERTEX_COLOR;
      pointSize = 8;
    }
    if (isSelectedFaceVertex) {
      color = SELECTED_FACE_VERTEX_COLOR;
      pointSize = 9;
    }
    if (isHovered) {
      color = HOVER_VERTEX_COLOR;
      pointSize = 10;
    }
    if (isSelected) {
      color = SELECTED_VERTEX_COLOR;
      pointSize = 11;
    }

    node.material.color.setHex(color);
    node.material.size = pointSize;
    node.material.opacity = isMuted ? 0.45 : 1;
    node.material.needsUpdate = true;

    // Dense models become unreadable if every label shows XYZ all the time.
    // Default visible labels are compact V# tags. Hover/selection/face contribution
    // expands only the relevant labels to the full V#(x,y,z) address.
    label.element.textContent = isEmphasized ? fullVertexLabel(vertexIndex) : `V${vertexIndex}`;
    label.element.title = fullVertexLabel(vertexIndex);

    label.element.classList.toggle("is-compact", !isEmphasized);
    label.element.classList.toggle("is-muted", isMuted);
    label.element.classList.toggle("is-hovered", isHovered);
    label.element.classList.toggle("is-selected-vertex", isSelected);
    label.element.classList.toggle("is-face-hover-vertex", isHoveredFaceVertex && !isSelectedFaceVertex);
    label.element.classList.toggle("is-face-vertex", isSelectedFaceVertex);
  }

  function updateVertexSet(indices: Iterable<number>): void {
    for (const index of indices) {
      updateVertexAppearance(index);
    }
  }

  function updateAllVertexAppearances(): void {
    vertexNodes.forEach((_, index) => updateVertexAppearance(index));
  }

  function setHoveredVertexInternal(vertexIndex: number | null, notify: boolean): void {
    if (hoveredVertex === vertexIndex) {
      return;
    }

    const previous = hoveredVertex;
    hoveredVertex = vertexIndex;

    if (previous !== null) {
      updateVertexAppearance(previous);
    }
    if (vertexIndex !== null) {
      updateVertexAppearance(vertexIndex);
    }

    if (notify) {
      handlers.onVertexHover?.(vertexIndex);
    }
  }

  function selectVertexInternal(vertexIndex: number | null, notify: boolean): void {
    if (vertexIndex !== null && !verticesVisible) {
      return;
    }
    if (selectedVertex === vertexIndex) {
      return;
    }

    if (vertexIndex !== null && selectedFace !== null) {
      selectFaceInternal(null, true);
    }

    const previous = selectedVertex;
    selectedVertex = vertexIndex;

    // A persistent vertex selection deliberately mutes all other visible vertices,
    // so every node may need a visual refresh.
    if (previous !== null || vertexIndex !== null) {
      updateAllVertexAppearances();
    }

    if (notify) {
      handlers.onVertexSelect?.(vertexIndex);
    }
  }

  function setHoveredFaceInternal(faceIndex: number | null, notify: boolean): void {
    if (hoveredFace === faceIndex) {
      return;
    }

    const previousFace = hoveredFace;
    const previousVertices = hoveredFaceVertices;

    hoveredFace = faceIndex;
    hoveredFaceVertices = faceVertices(faceIndex);

    if (previousFace !== null) {
      updateFaceAppearance(previousFace);
    }
    if (faceIndex !== null) {
      updateFaceAppearance(faceIndex);
    }

    updateVertexSet(new Set<number>([
      ...previousVertices,
      ...hoveredFaceVertices,
    ]));

    if (notify) {
      handlers.onFaceHover?.(faceIndex);
    }
  }

  function selectFaceInternal(faceIndex: number | null, notify: boolean): void {
    if (faceIndex !== null && !faceSelectMode) {
      return;
    }
    if (selectedFace === faceIndex) {
      return;
    }

    if (faceIndex !== null && selectedVertex !== null) {
      selectVertexInternal(null, true);
    }

    const previousFace = selectedFace;
    const previousVertices = selectedFaceVertices;

    selectedFace = faceIndex;
    selectedFaceVertices = faceVertices(faceIndex);

    if (previousFace !== null) {
      updateFaceAppearance(previousFace);
    }
    if (faceIndex !== null) {
      updateFaceAppearance(faceIndex);
    }

    updateVertexSet(new Set<number>([
      ...previousVertices,
      ...selectedFaceVertices,
    ]));

    if (notify) {
      handlers.onFaceSelect?.(faceIndex);
    }
  }

  function clear(): void {
    setHoveredVertexInternal(null, false);
    setHoveredFaceInternal(null, false);

    selectedVertex = null;
    selectedFace = null;
    selectedFaceVertices = new Set<number>();
    hoveredFaceVertices = new Set<number>();

    if (modelRoot) {
      scene.remove(modelRoot);
      for (const mesh of faceMeshes) {
        mesh.geometry.dispose();
        mesh.material.dispose();
      }
      faceMeshes.length = 0;

      if (edgeGeometry) {
        edgeGeometry.dispose();
        edgeGeometry = undefined;
      }
      if (edgeMaterial) {
        edgeMaterial.dispose();
        edgeMaterial = undefined;
      }

      modelRoot = undefined;
    }

    if (vertexRoot) {
      scene.remove(vertexRoot);
      for (const node of vertexNodes) {
        node.material.dispose();
      }
      for (const label of vertexLabels) {
        label.element.remove();
      }
      vertexNodes.length = 0;
      vertexLabels.length = 0;
      vertexPointGeometry?.dispose();
      vertexPointGeometry = undefined;
      vertexRoot = undefined;
    }

    if (grid) {
      scene.remove(grid);
      grid.geometry.dispose();
      if (Array.isArray(grid.material)) {
        grid.material.forEach((material: THREE.Material) => material.dispose());
      } else {
        grid.material.dispose();
      }
      grid = undefined;
    }

    currentParsed = undefined;
  }

  function fit(): void {
    if (!currentParsed) {
      return;
    }

    const vertices = currentParsed.geometry.brep.verticesWorld;
    if (vertices.length === 0) {
      return;
    }

    const box = new THREE.Box3();
    for (const vertex of vertices) {
      box.expandByPoint(new THREE.Vector3(vertex.x, vertex.y, vertex.z));
    }

    const size = new THREE.Vector3();
    const center = new THREE.Vector3();
    box.getSize(size);
    box.getCenter(center);

    const maxDimension = Math.max(size.x, size.y, size.z, 1);
    const fovRadians = THREE.MathUtils.degToRad(camera.fov);
    const distance = (maxDimension / (2 * Math.tan(fovRadians / 2))) * 1.8;

    camera.position.set(
      center.x + distance * 0.8,
      center.y - distance * 0.9,
      center.z + distance * 0.65,
    );

    camera.near = Math.max(distance / 10000, 0.1);
    camera.far = distance * 100;
    camera.updateProjectionMatrix();

    controls.target.copy(center);
    controls.update();
  }

  function load(parsed: ParsedSmd): void {
    clear();
    currentParsed = parsed;

    const root = new THREE.Group();
    root.name = "smd-model";

    parsed.geometry.brep.faces.forEach((_, faceIndex) => {
      const geometry = buildFaceGeometry(parsed, faceIndex);
      const material = new THREE.MeshStandardMaterial({
        color: faceColorsEnabled ? faceColor(faceIndex) : DEFAULT_FACE_COLOR,
        roughness: 0.82,
        metalness: 0,
        side: THREE.DoubleSide,
      });
      const mesh = new THREE.Mesh(geometry, material);
      mesh.userData.faceIndex = faceIndex;
      mesh.name = `SMD Face F${faceIndex}`;
      faceMeshes.push(mesh);
      root.add(mesh);
    });

    const combinedGeometry = buildGeometry(parsed);
    edgeGeometry = new THREE.EdgesGeometry(combinedGeometry, 1);
    combinedGeometry.dispose();
    edgeMaterial = new THREE.LineBasicMaterial({ color: 0x222222 });
    const edges = new THREE.LineSegments(edgeGeometry, edgeMaterial);
    edges.name = "SMD edges";
    root.add(edges);

    modelRoot = root;
    scene.add(root);

    const vertices = parsed.geometry.brep.verticesWorld;
    const box = new THREE.Box3();
    for (const vertex of vertices) {
      box.expandByPoint(new THREE.Vector3(vertex.x, vertex.y, vertex.z));
    }
    const size = new THREE.Vector3();
    box.getSize(size);
    const maxDimension = Math.max(size.x, size.y, size.z, 1);

    // Raycaster point thresholds are world-space even though the visible point
    // marker is screen-sized. Scale the hit tolerance from the model extent.
    raycaster.params.Points.threshold = Math.max(maxDimension * 0.01, 12);

    vertexPointGeometry = new THREE.BufferGeometry();
    vertexPointGeometry.setAttribute(
      "position",
      new THREE.Float32BufferAttribute([0, 0, 0], 3),
    );

    vertexRoot = new THREE.Group();
    vertexRoot.name = "vertex-inspector";

    vertices.forEach((vertex, vertexIndex) => {
      const material = new THREE.PointsMaterial({
        color: DEFAULT_VERTEX_COLOR,
        size: 5.5,
        sizeAttenuation: false,
        transparent: true,
        opacity: 1,
        depthTest: false,
        depthWrite: false,
      });
      const node = new THREE.Points(vertexPointGeometry!, material);
      node.position.set(vertex.x, vertex.y, vertex.z);
      node.userData.vertexIndex = vertexIndex;
      node.name = `Vertex V${vertexIndex}`;
      node.visible = verticesVisible;
      node.renderOrder = 20;
      vertexNodes.push(node);
      vertexRoot!.add(node);

      const labelElement = document.createElement("div");
      labelElement.className = "vertex-label is-compact";
      labelElement.dataset.vertexIndex = String(vertexIndex);
      labelElement.textContent = `V${vertexIndex}`;
      labelElement.title = fullVertexLabel(vertexIndex);

      // Labels themselves participate in the same hover/selection behaviour as dots.
      labelElement.addEventListener("mouseenter", () => {
        setHoveredVertexInternal(vertexIndex, true);
      });
      labelElement.addEventListener("mouseleave", () => {
        setHoveredVertexInternal(null, true);
      });
      labelElement.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        if (!verticesVisible) {
          return;
        }
        const next = selectedVertex === vertexIndex ? null : vertexIndex;
        selectVertexInternal(next, true);
      });

      const label = new CSS2DObject(labelElement);
      label.position.set(vertex.x, vertex.y, vertex.z);
      label.visible = verticesVisible;
      vertexLabels.push(label);
      vertexRoot!.add(label);
    });

    scene.add(vertexRoot);

    grid = makeGrid(maxDimension * 2.5);
    scene.add(grid);

    updateAllFaceAppearances();
    updateAllVertexAppearances();
    fit();
  }

  function resize(): void {
    const width = Math.max(container.clientWidth, 1);
    const height = Math.max(container.clientHeight, 1);
    renderer.setSize(width, height, false);
    labelRenderer.setSize(width, height);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
  }

  function updatePointer(event: PointerEvent): void {
    const rect = renderer.domElement.getBoundingClientRect();
    pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
    raycaster.setFromCamera(pointer, camera);
  }

  function visibleVertexNodes(): THREE.Object3D[] {
    return vertexNodes.filter((node) => node.visible);
  }

  function vertexHitAtPointer(): THREE.Intersection | undefined {
    const visibleNodes = visibleVertexNodes();
    if (visibleNodes.length === 0) {
      return undefined;
    }
    return raycaster.intersectObjects(visibleNodes, false)[0];
  }

  function handlePointerMove(event: PointerEvent): void {
    updatePointer(event);

    const vertexHit = vertexHitAtPointer();
    if (vertexHit) {
      const vertexIndex = Number(vertexHit.object.userData.vertexIndex);
      setHoveredVertexInternal(vertexIndex, true);
      renderer.domElement.style.cursor = "pointer";
      return;
    }

    setHoveredVertexInternal(null, true);

    if (faceSelectMode && faceMeshes.length > 0) {
      const faceHit = raycaster.intersectObjects(faceMeshes, false)[0];
      if (faceHit) {
        const faceIndex = Number(faceHit.object.userData.faceIndex);
        setHoveredFaceInternal(faceIndex, true);
        renderer.domElement.style.cursor = "pointer";
        return;
      }
    }

    if (faceSelectMode) {
      setHoveredFaceInternal(null, true);
    }
    renderer.domElement.style.cursor = faceSelectMode ? "crosshair" : "grab";
  }

  let pointerDownX = 0;
  let pointerDownY = 0;

  function handlePointerDown(event: PointerEvent): void {
    pointerDownX = event.clientX;
    pointerDownY = event.clientY;
  }

  function handlePointerUp(event: PointerEvent): void {
    const movement = Math.hypot(event.clientX - pointerDownX, event.clientY - pointerDownY);
    if (movement > 5) {
      return;
    }

    updatePointer(event);

    // Vertex selection is available only while Show vertices is ON.
    if (verticesVisible) {
      const vertexHit = vertexHitAtPointer();
      if (vertexHit) {
        const vertexIndex = Number(vertexHit.object.userData.vertexIndex);
        const next = selectedVertex === vertexIndex ? null : vertexIndex;
        selectVertexInternal(next, true);
        return;
      }
    }

    // Clicking anywhere else in the 3D canvas clears a persistent vertex selection.
    if (selectedVertex !== null) {
      selectVertexInternal(null, true);
    }

    // Persistent face selection exists only while Face select is ON.
    if (faceSelectMode) {
      const faceHit = raycaster.intersectObjects(faceMeshes, false)[0];
      if (faceHit) {
        const faceIndex = Number(faceHit.object.userData.faceIndex);
        const next = selectedFace === faceIndex ? null : faceIndex;
        selectFaceInternal(next, true);
      } else {
        selectFaceInternal(null, true);
      }
    }
  }

  function handlePointerLeave(): void {
    setHoveredVertexInternal(null, true);
    if (faceSelectMode) {
      setHoveredFaceInternal(null, true);
    }
    renderer.domElement.style.cursor = faceSelectMode ? "crosshair" : "grab";
  }

  renderer.domElement.addEventListener("pointermove", handlePointerMove);
  renderer.domElement.addEventListener("pointerdown", handlePointerDown);
  renderer.domElement.addEventListener("pointerup", handlePointerUp);
  renderer.domElement.addEventListener("pointerleave", handlePointerLeave);

  const resizeObserver = new ResizeObserver(resize);
  resizeObserver.observe(container);
  resize();

  let animationFrame = 0;
  const animate = (): void => {
    animationFrame = requestAnimationFrame(animate);
    controls.update();
    renderer.render(scene, camera);
    labelRenderer.render(scene, camera);
  };
  animate();

  return {
    load,
    clear,
    fit,
    setVerticesVisible(visible: boolean): void {
      verticesVisible = visible;
      if (!visible && selectedVertex !== null) {
        selectVertexInternal(null, true);
      }
      updateAllVertexAppearances();
    },
    setFaceSelectMode(enabled: boolean): void {
      faceSelectMode = enabled;
      if (!enabled) {
        // Leaving face-select mode must return all persistent face state to neutral.
        setHoveredFaceInternal(null, true);
        selectFaceInternal(null, true);
      }
      renderer.domElement.style.cursor = enabled ? "crosshair" : "grab";
    },
    setFaceColors(enabled: boolean): void {
      faceColorsEnabled = enabled;
      updateAllFaceAppearances();
    },
    setHoveredVertex(vertexIndex: number | null): void {
      setHoveredVertexInternal(vertexIndex, false);
    },
    setHoveredFace(faceIndex: number | null): void {
      setHoveredFaceInternal(faceIndex, false);
    },
    selectVertex(vertexIndex: number | null): void {
      selectVertexInternal(vertexIndex, false);
    },
    selectFace(faceIndex: number | null): void {
      selectFaceInternal(faceIndex, false);
    },
    setInteractionHandlers(nextHandlers: ViewerInteractionHandlers): void {
      handlers = nextHandlers;
    },
    dispose(): void {
      cancelAnimationFrame(animationFrame);
      resizeObserver.disconnect();
      renderer.domElement.removeEventListener("pointermove", handlePointerMove);
      renderer.domElement.removeEventListener("pointerdown", handlePointerDown);
      renderer.domElement.removeEventListener("pointerup", handlePointerUp);
      renderer.domElement.removeEventListener("pointerleave", handlePointerLeave);
      clear();
      controls.dispose();
      renderer.dispose();
      renderer.domElement.remove();
      labelRenderer.domElement.remove();
    },
  };
}

export function boundingBoxText(vertices: Vec3[]): string {
  if (vertices.length === 0) {
    return "-";
  }

  let minX = Infinity;
  let minY = Infinity;
  let minZ = Infinity;
  let maxX = -Infinity;
  let maxY = -Infinity;
  let maxZ = -Infinity;

  for (const v of vertices) {
    minX = Math.min(minX, v.x);
    minY = Math.min(minY, v.y);
    minZ = Math.min(minZ, v.z);
    maxX = Math.max(maxX, v.x);
    maxY = Math.max(maxY, v.y);
    maxZ = Math.max(maxZ, v.z);
  }

  return `X ${minX}..${maxX} | Y ${minY}..${maxY} | Z ${minZ}..${maxZ} mm`;
}
