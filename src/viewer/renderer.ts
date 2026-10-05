import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import type { ParsedSmd, Vec3 } from "../smd/types";
import { triangulateFace } from "./triangulation";

export interface ViewerHandle {
  load(parsed: ParsedSmd): void;
  clear(): void;
  fit(): void;
  dispose(): void;
}

function buildGeometry(parsed: ParsedSmd): THREE.BufferGeometry {
  const vertices = parsed.geometry.brep.vertices;
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

  geometry.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(positions, 3),
  );

  geometry.computeVertexNormals();
  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();

  return geometry;
}

function makeGrid(size: number): THREE.GridHelper {
  const safeSize = Math.max(size, 1000);
  const divisions = 20;

  const grid = new THREE.GridHelper(safeSize, divisions);

  // Three.js GridHelper lies on XZ.
  // STRAKON uses Z as vertical, so rotate to XY.
  grid.rotation.x = Math.PI / 2;

  return grid;
}

export function createViewer(container: HTMLElement): ViewerHandle {
  THREE.Object3D.DEFAULT_UP.set(0, 0, 1);

  // ---------------------------------------------------------------------------
  // Scene
  // ---------------------------------------------------------------------------

  const scene = new THREE.Scene();

  scene.background = new THREE.Color(0xf4f6f8);

  // ---------------------------------------------------------------------------
  // Camera
  // ---------------------------------------------------------------------------

  const camera = new THREE.PerspectiveCamera(
    45,
    1,
    1,
    1_000_000,
  );

  camera.up.set(0, 0, 1);

  // ---------------------------------------------------------------------------
  // Renderer
  // ---------------------------------------------------------------------------

  const renderer = new THREE.WebGLRenderer({
    antialias: true,
  });

  renderer.setPixelRatio(
    Math.min(window.devicePixelRatio, 2),
  );

  container.appendChild(renderer.domElement);

  // ---------------------------------------------------------------------------
  // Controls
  // ---------------------------------------------------------------------------

  const controls = new OrbitControls(
    camera,
    renderer.domElement,
  );

  controls.enableDamping = true;
  controls.dampingFactor = 0.08;

  // ---------------------------------------------------------------------------
  // Lights
  // ---------------------------------------------------------------------------

  const ambient = new THREE.AmbientLight(
    0xffffff,
    1.8,
  );

  scene.add(ambient);

  const keyLight = new THREE.DirectionalLight(
    0xffffff,
    2.3,
  );

  keyLight.position.set(1, -2, 3);
  scene.add(keyLight);

  const fillLight = new THREE.DirectionalLight(
    0xffffff,
    1.2,
  );

  fillLight.position.set(-2, 1, 1);
  scene.add(fillLight);

  // ---------------------------------------------------------------------------
  // Axes
  // ---------------------------------------------------------------------------

  const axes = new THREE.AxesHelper(1000);

  scene.add(axes);

  // ---------------------------------------------------------------------------
  // Viewer state
  // ---------------------------------------------------------------------------

  let modelRoot: THREE.Group | undefined;
  let grid: THREE.GridHelper | undefined;

  /**
   * Original STRAKON/BREP vertices.
   *
   * This is kept separately because buildGeometry() duplicates
   * vertices when creating triangle positions.
   */
  let parsedVertices: Vec3[] = [];

  // ---------------------------------------------------------------------------
  // Vertex hover state
  // ---------------------------------------------------------------------------

  let vertexPoints: THREE.Points | undefined;

  let hoveredVertexIndex: number | undefined;

  // Raycaster used for vertex detection.
  const raycaster = new THREE.Raycaster();

  // Mouse position in normalized device coordinates.
  const mouse = new THREE.Vector2();

  // How close the mouse has to be to a vertex.
  //
  // Because this is a Points object, this value is in world units.
  // We will also dynamically scale it based on camera distance.
  raycaster.params.Points.threshold = 10;

  // ---------------------------------------------------------------------------
  // Tooltip
  // ---------------------------------------------------------------------------

  const tooltip = document.createElement("div");

  tooltip.style.position = "absolute";
  tooltip.style.display = "none";
  tooltip.style.pointerEvents = "none";
  tooltip.style.zIndex = "1000";

  tooltip.style.padding = "8px 10px";

  tooltip.style.background = "rgba(20, 20, 20, 0.92)";
  tooltip.style.color = "#ffffff";

  tooltip.style.border = "1px solid rgba(255, 255, 255, 0.2)";
  tooltip.style.borderRadius = "5px";

  tooltip.style.fontFamily = "monospace";
  tooltip.style.fontSize = "12px";
  tooltip.style.lineHeight = "1.4";

  tooltip.style.whiteSpace = "pre";

  tooltip.style.boxShadow =
    "0 2px 8px rgba(0, 0, 0, 0.25)";

  // Make sure the tooltip can be positioned relative to the viewer.
  if (getComputedStyle(container).position === "static") {
    container.style.position = "relative";
  }

  container.appendChild(tooltip);

  // ---------------------------------------------------------------------------
  // Clear
  // ---------------------------------------------------------------------------

  function clear(): void {
    if (modelRoot) {
      scene.remove(modelRoot);

      modelRoot.traverse((object: THREE.Object3D) => {
        if (
          object instanceof THREE.Mesh ||
          object instanceof THREE.LineSegments ||
          object instanceof THREE.Points
        ) {
          object.geometry.dispose();

          if (Array.isArray(object.material)) {
            object.material.forEach(
              (material: THREE.Material) => {
                material.dispose();
              },
            );
          } else {
            object.material.dispose();
          }
        }
      });

      modelRoot = undefined;
      vertexPoints = undefined;
    }

    if (grid) {
      scene.remove(grid);

      grid.geometry.dispose();

      if (Array.isArray(grid.material)) {
        grid.material.forEach(
          (material: THREE.Material) => {
            material.dispose();
          },
        );
      } else {
        grid.material.dispose();
      }

      grid = undefined;
    }

    parsedVertices = [];

    hoveredVertexIndex = undefined;

    tooltip.style.display = "none";
  }

  // ---------------------------------------------------------------------------
  // Fit camera
  // ---------------------------------------------------------------------------

  function fit(): void {
    if (!modelRoot) {
      return;
    }

    const box = new THREE.Box3().setFromObject(
      modelRoot,
    );

    if (box.isEmpty()) {
      return;
    }

    const size = new THREE.Vector3();
    const center = new THREE.Vector3();

    box.getSize(size);
    box.getCenter(center);

    const maxDimension = Math.max(
      size.x,
      size.y,
      size.z,
      1,
    );

    const fovRadians = THREE.MathUtils.degToRad(
      camera.fov,
    );

    const distance =
      (maxDimension /
        (2 * Math.tan(fovRadians / 2))) *
      1.8;

    camera.position.set(
      center.x + distance * 0.8,
      center.y - distance * 0.9,
      center.z + distance * 0.65,
    );

    camera.near = Math.max(
      distance / 10000,
      0.1,
    );

    camera.far = distance * 100;

    camera.updateProjectionMatrix();

    controls.target.copy(center);

    controls.update();
  }

  // ---------------------------------------------------------------------------
  // Build vertex points
  // ---------------------------------------------------------------------------

  function buildVertexPoints(
    vertices: Vec3[],
  ): THREE.Points {
    const positions = new Float32Array(
      vertices.length * 3,
    );

    for (let i = 0; i < vertices.length; i++) {
      const vertex = vertices[i];

      positions[i * 3] = vertex.x;
      positions[i * 3 + 1] = vertex.y;
      positions[i * 3 + 2] = vertex.z;
    }

    const geometry = new THREE.BufferGeometry();

    geometry.setAttribute(
      "position",
      new THREE.BufferAttribute(
        positions,
        3,
      ),
    );

    const material = new THREE.PointsMaterial({
      color: 0xff3b30,

      // Visible vertex marker.
      size: 8,

      // Keeps marker approximately the same screen size.
      sizeAttenuation: false,
    });

    return new THREE.Points(
      geometry,
      material,
    );
  }

  // ---------------------------------------------------------------------------
  // Highlight hovered vertex
  // ---------------------------------------------------------------------------

  function updateVertexHighlight(
    vertexIndex: number | undefined,
  ): void {
    if (!vertexPoints) {
      return;
    }

    const material =
      vertexPoints.material as THREE.PointsMaterial;

    if (vertexIndex === undefined) {
      material.color.setHex(0xff3b30);
      material.size = 8;

      return;
    }

    // Yellow when hovered.
    material.color.setHex(0xffcc00);
    material.size = 12;
  }

  // ---------------------------------------------------------------------------
  // Show tooltip
  // ---------------------------------------------------------------------------

  function showVertexTooltip(
    vertexIndex: number,
    event: PointerEvent,
  ): void {
    const vertex = parsedVertices[vertexIndex];

    if (!vertex) {
      return;
    }

    tooltip.textContent =
      `Vertex ${vertexIndex}\n` +
      `X: ${vertex.x.toFixed(2)} mm\n` +
      `Y: ${vertex.y.toFixed(2)} mm\n` +
      `Z: ${vertex.z.toFixed(2)} mm`;

    const rect =
      renderer.domElement.getBoundingClientRect();

    tooltip.style.left =
      `${event.clientX - rect.left + 12}px`;

    tooltip.style.top =
      `${event.clientY - rect.top + 12}px`;

    tooltip.style.display = "block";
  }

  // ---------------------------------------------------------------------------
  // Hide tooltip
  // ---------------------------------------------------------------------------

  function hideVertexTooltip(): void {
    tooltip.style.display = "none";

    if (hoveredVertexIndex !== undefined) {
      hoveredVertexIndex = undefined;

      updateVertexHighlight(undefined);
    }
  }

  // ---------------------------------------------------------------------------
  // Pointer move
  // ---------------------------------------------------------------------------

  function onPointerMove(
    event: PointerEvent,
  ): void {
    if (!vertexPoints || parsedVertices.length === 0) {
      hideVertexTooltip();
      return;
    }

    const rect =
      renderer.domElement.getBoundingClientRect();

    mouse.x =
      ((event.clientX - rect.left) /
        rect.width) *
        2 -
      1;

    mouse.y =
      -(
        ((event.clientY - rect.top) /
          rect.height) *
          2 -
        1
      );

    raycaster.setFromCamera(
      mouse,
      camera,
    );

    // -----------------------------------------------------------------------
    // Dynamically adjust point picking threshold.
    //
    // This makes vertex selection easier when zoomed out and more precise
    // when zoomed in.
    // -----------------------------------------------------------------------

    const distance =
      camera.position.distanceTo(
        controls.target,
      );

    raycaster.params.Points.threshold =
      Math.max(
        distance * 0.005,
        1,
      );

    const intersections =
      raycaster.intersectObject(
        vertexPoints,
        false,
      );

    if (intersections.length === 0) {
      hideVertexTooltip();
      return;
    }

    const intersection =
      intersections[0];

    if (intersection.index === undefined) {
      hideVertexTooltip();
      return;
    }

    const vertexIndex =
      intersection.index;

    // -----------------------------------------------------------------------
    // New hovered vertex
    // -----------------------------------------------------------------------

    if (
      hoveredVertexIndex !==
      vertexIndex
    ) {
      hoveredVertexIndex =
        vertexIndex;

      updateVertexHighlight(
        vertexIndex,
      );
    }

    showVertexTooltip(
      vertexIndex,
      event,
    );
  }

  // ---------------------------------------------------------------------------
  // Pointer leave
  // ---------------------------------------------------------------------------

  function onPointerLeave(): void {
    hideVertexTooltip();
  }

  renderer.domElement.addEventListener(
    "pointermove",
    onPointerMove,
  );

  renderer.domElement.addEventListener(
    "pointerleave",
    onPointerLeave,
  );

  // ---------------------------------------------------------------------------
  // Load model
  // ---------------------------------------------------------------------------

  function load(
    parsed: ParsedSmd,
  ): void {
    clear();

    // Keep the original vertices for tooltip information.
    parsedVertices =
      parsed.geometry.brep.vertices;

    // -------------------------------------------------------------------------
    // Main mesh
    // -------------------------------------------------------------------------

    const geometry =
      buildGeometry(parsed);

    const root =
      new THREE.Group();

    const material =
      new THREE.MeshStandardMaterial({
        color: 0xc9cdd2,
        roughness: 0.82,
        metalness: 0,
        side: THREE.DoubleSide,
      });

    const mesh =
      new THREE.Mesh(
        geometry,
        material,
      );

    root.add(mesh);

    // -------------------------------------------------------------------------
    // Edges
    // -------------------------------------------------------------------------

    const edgeGeometry =
      new THREE.EdgesGeometry(
        geometry,
        1,
      );

    const edgeMaterial =
      new THREE.LineBasicMaterial({
        color: 0x222222,
      });

    const edges =
      new THREE.LineSegments(
        edgeGeometry,
        edgeMaterial,
      );

    root.add(edges);

    // -------------------------------------------------------------------------
    // Original vertices
    // -------------------------------------------------------------------------

    vertexPoints =
      buildVertexPoints(
        parsedVertices,
      );

    root.add(vertexPoints);

    // -------------------------------------------------------------------------
    // Add model
    // -------------------------------------------------------------------------

    modelRoot = root;

    scene.add(root);

    // -------------------------------------------------------------------------
    // Grid
    // -------------------------------------------------------------------------

    const box =
      new THREE.Box3().setFromObject(
        root,
      );

    const size =
      new THREE.Vector3();

    box.getSize(size);

    grid = makeGrid(
      Math.max(
        size.x,
        size.y,
        size.z,
      ) * 2.5,
    );

    scene.add(grid);

    // -------------------------------------------------------------------------
    // Fit camera
    // -------------------------------------------------------------------------

    fit();
  }

  // ---------------------------------------------------------------------------
  // Resize
  // ---------------------------------------------------------------------------

  function resize(): void {
    const width =
      Math.max(
        container.clientWidth,
        1,
      );

    const height =
      Math.max(
        container.clientHeight,
        1,
      );

    renderer.setSize(
      width,
      height,
      false,
    );

    camera.aspect =
      width / height;

    camera.updateProjectionMatrix();
  }

  const resizeObserver =
    new ResizeObserver(
      resize,
    );

  resizeObserver.observe(
    container,
  );

  resize();

  // ---------------------------------------------------------------------------
  // Animation
  // ---------------------------------------------------------------------------

  let animationFrame = 0;

  const animate = (): void => {
    animationFrame =
      requestAnimationFrame(
        animate,
      );

    controls.update();

    renderer.render(
      scene,
      camera,
    );
  };

  animate();

  // ---------------------------------------------------------------------------
  // Viewer API
  // ---------------------------------------------------------------------------

  return {
    load,

    clear,

    fit,

    dispose(): void {
      cancelAnimationFrame(
        animationFrame,
      );

      resizeObserver.disconnect();

      renderer.domElement.removeEventListener(
        "pointermove",
        onPointerMove,
      );

      renderer.domElement.removeEventListener(
        "pointerleave",
        onPointerLeave,
      );

      clear();

      tooltip.remove();

      controls.dispose();

      renderer.dispose();

      renderer.domElement.remove();
    },
  };
}

// -----------------------------------------------------------------------------
// Bounding box text
// -----------------------------------------------------------------------------

export function boundingBoxText(
  vertices: Vec3[],
): string {
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

  return (
    `X ${minX}..${maxX} | ` +
    `Y ${minY}..${maxY} | ` +
    `Z ${minZ}..${maxZ} mm`
  );
}
