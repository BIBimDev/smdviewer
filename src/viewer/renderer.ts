import * as THREE from "three";

import {
  OrbitControls
} from "three/examples/jsm/controls/OrbitControls.js";

import {
  CSS2DObject,
  CSS2DRenderer
} from "three/examples/jsm/renderers/CSS2DRenderer.js";

import type {
  Face,
  ParsedSmd,
  Vec3
} from "../smd/types";

import {
  triangulateFace
} from "./triangulation";


export interface ViewerHandle {

  load(
    parsed: ParsedSmd
  ): void;

  clear(): void;

  fit(): void;

  setVerticesVisible(
    visible: boolean
  ): void;

  setHoveredVertex(
    index: number | null
  ): void;

  setVertexHoverHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void;

  setFaceSelectMode(
    enabled: boolean
  ): void;

  setHoveredFace(
    index: number | null
  ): void;

  setSelectedFace(
    index: number | null
  ): void;

  setFaceHoverHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void;

  setFaceSelectionHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void;

  dispose(): void;
}


const DEFAULT_VERTEX_COLOR =
  0x2563eb;

const HOVER_VERTEX_COLOR =
  0xf59e0b;

const FACE_VERTEX_COLOR =
  0xdc2626;

const DEFAULT_FACE_COLOR =
  0xc9cdd2;

const HOVER_FACE_COLOR =
  0xfbbf24;

const SELECTED_FACE_COLOR =
  0x60a5fa;


type VertexNode =
  THREE.Mesh<
    THREE.SphereGeometry,
    THREE.MeshBasicMaterial
  >;


type FaceNode =
  THREE.Mesh<
    THREE.BufferGeometry,
    THREE.MeshStandardMaterial
  >;


function formatCoordinate(
  value: number
): string {

  const normalized =
    Math.abs(value) < 1e-9
      ? 0
      : value;

  const rounded =
    Math.round(normalized);

  if (
    Math.abs(
      normalized - rounded
    ) < 1e-6
  ) {
    return String(
      rounded
    );
  }

  return normalized.toFixed(3);
}


function vertexLabelText(
  index: number,
  vertex: Vec3
): string {

  return (
    `V${index} (` +
    `${formatCoordinate(vertex.x)}, ` +
    `${formatCoordinate(vertex.y)}, ` +
    `${formatCoordinate(vertex.z)})`
  );
}


function buildCombinedGeometry(
  parsed: ParsedSmd
): THREE.BufferGeometry {

  const vertices =
    parsed.geometry.brep.verticesWorld;

  const positions: number[] =
    [];

  for (
    const face of
    parsed.geometry.brep.faces
  ) {

    const triangles =
      triangulateFace(
        vertices,
        face
      );

    for (
      const triangle of triangles
    ) {

      for (
        const vertexIndex of triangle
      ) {

        const vertex =
          vertices[vertexIndex];

        positions.push(
          vertex.x,
          vertex.y,
          vertex.z
        );
      }
    }
  }

  const geometry =
    new THREE.BufferGeometry();

  geometry.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(
      positions,
      3
    )
  );

  geometry.computeVertexNormals();
  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();

  return geometry;
}


function buildFaceGeometry(
  vertices: Vec3[],
  face: Face
): THREE.BufferGeometry {

  const positions: number[] =
    [];

  const triangles =
    triangulateFace(
      vertices,
      face
    );

  for (
    const triangle of triangles
  ) {

    for (
      const vertexIndex of triangle
    ) {

      const vertex =
        vertices[vertexIndex];

      positions.push(
        vertex.x,
        vertex.y,
        vertex.z
      );
    }
  }

  const geometry =
    new THREE.BufferGeometry();

  geometry.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(
      positions,
      3
    )
  );

  geometry.computeVertexNormals();
  geometry.computeBoundingBox();
  geometry.computeBoundingSphere();

  return geometry;
}


function makeGrid(
  size: number
): THREE.GridHelper {

  const safeSize =
    Math.max(
      size,
      1000
    );

  const grid =
    new THREE.GridHelper(
      safeSize,
      20
    );

  grid.rotation.x =
    Math.PI / 2;

  return grid;
}


function getModelSize(
  vertices: Vec3[]
): number {

  if (
    vertices.length === 0
  ) {
    return 1000;
  }

  let minX = Infinity;
  let minY = Infinity;
  let minZ = Infinity;

  let maxX = -Infinity;
  let maxY = -Infinity;
  let maxZ = -Infinity;

  for (
    const vertex of vertices
  ) {

    minX =
      Math.min(
        minX,
        vertex.x
      );

    minY =
      Math.min(
        minY,
        vertex.y
      );

    minZ =
      Math.min(
        minZ,
        vertex.z
      );

    maxX =
      Math.max(
        maxX,
        vertex.x
      );

    maxY =
      Math.max(
        maxY,
        vertex.y
      );

    maxZ =
      Math.max(
        maxZ,
        vertex.z
      );
  }

  return Math.max(
    maxX - minX,
    maxY - minY,
    maxZ - minZ,
    1
  );
}


export function createViewer(
  container: HTMLElement
): ViewerHandle {

  THREE.Object3D.DEFAULT_UP.set(
    0,
    0,
    1
  );

  const scene =
    new THREE.Scene();

  scene.background =
    new THREE.Color(
      0xf4f6f8
    );


  const camera =
    new THREE.PerspectiveCamera(
      45,
      1,
      1,
      1_000_000
    );

  camera.up.set(
    0,
    0,
    1
  );


  const renderer =
    new THREE.WebGLRenderer({
      antialias: true
    });

  renderer.setPixelRatio(
    Math.min(
      window.devicePixelRatio,
      2
    )
  );

  container.appendChild(
    renderer.domElement
  );


  const labelRenderer =
    new CSS2DRenderer();

  labelRenderer.domElement.className =
    "vertex-label-layer";

  labelRenderer.domElement.style.position =
    "absolute";

  labelRenderer.domElement.style.left =
    "0";

  labelRenderer.domElement.style.top =
    "0";

  labelRenderer.domElement.style.pointerEvents =
    "none";

  container.appendChild(
    labelRenderer.domElement
  );


  const controls =
    new OrbitControls(
      camera,
      renderer.domElement
    );

  controls.enableDamping =
    true;

  controls.dampingFactor =
    0.08;


  const ambient =
    new THREE.AmbientLight(
      0xffffff,
      1.8
    );

  scene.add(
    ambient
  );


  const keyLight =
    new THREE.DirectionalLight(
      0xffffff,
      2.3
    );

  keyLight.position.set(
    1,
    -2,
    3
  );

  scene.add(
    keyLight
  );


  const fillLight =
    new THREE.DirectionalLight(
      0xffffff,
      1.2
    );

  fillLight.position.set(
    -2,
    1,
    1
  );

  scene.add(
    fillLight
  );


  const axes =
    new THREE.AxesHelper(
      1000
    );

  scene.add(
    axes
  );


  const raycaster =
    new THREE.Raycaster();

  const pointer =
    new THREE.Vector2();


  let modelRoot:
    THREE.Group | undefined;

  let vertexRoot:
    THREE.Group | undefined;

  let grid:
    THREE.GridHelper | undefined;


  let verticesVisible =
    false;

  let faceSelectMode =
    false;


  let hoveredVertexIndex:
    number | null =
    null;

  let hoveredFaceIndex:
    number | null =
    null;

  let selectedFaceIndex:
    number | null =
    null;


  let lastViewerHoverIndex:
    number | null =
    null;

  let lastViewerFaceHoverIndex:
    number | null =
    null;


  let vertexHoverHandler:
    (
      index: number | null
    ) => void =
    () => undefined;


  let faceHoverHandler:
    (
      index: number | null
    ) => void =
    () => undefined;


  let faceSelectionHandler:
    (
      index: number | null
    ) => void =
    () => undefined;


  const vertexNodes:
    VertexNode[] =
    [];

  const vertexLabels:
    HTMLElement[] =
    [];

  const vertexLabelObjects:
    CSS2DObject[] =
    [];

  const faceMeshes:
    FaceNode[] =
    [];

  const faceVertexIndices:
    number[][] =
    [];


  function activeFaceVertexSet():
    Set<number> {

    const activeFace =
      hoveredFaceIndex !== null
        ? hoveredFaceIndex
        : selectedFaceIndex;

    if (
      activeFace === null
      ||
      activeFace < 0
      ||
      activeFace >=
        faceVertexIndices.length
    ) {
      return new Set<number>();
    }

    return new Set<number>(
      faceVertexIndices[
        activeFace
      ]
    );
  }


  function applyVertexAppearance(): void {

    const faceVertices =
      activeFaceVertexSet();

    vertexNodes.forEach(
      (
        node,
        index
      ) => {

        const isHovered =
          index ===
          hoveredVertexIndex;

        const isFaceVertex =
          faceVertices.has(
            index
          );

        node.visible =
          verticesVisible
          ||
          isFaceVertex;

        if (
          isHovered
        ) {

          node.material.color.setHex(
            HOVER_VERTEX_COLOR
          );

          node.scale.setScalar(
            2.5
          );

        } else if (
          isFaceVertex
        ) {

          node.material.color.setHex(
            FACE_VERTEX_COLOR
          );

          node.scale.setScalar(
            1.8
          );

        } else {

          node.material.color.setHex(
            DEFAULT_VERTEX_COLOR
          );

          node.scale.setScalar(
            1
          );
        }
      }
    );


    vertexLabels.forEach(
      (
        label,
        index
      ) => {

        label.classList.toggle(
          "is-hovered",
          index ===
            hoveredVertexIndex
        );

        label.classList.toggle(
          "is-face-related",
          faceVertices.has(
            index
          )
        );
      }
    );


    vertexLabelObjects.forEach(
      (
        labelObject,
        index
      ) => {

        labelObject.visible =
          verticesVisible
          ||
          faceVertices.has(
            index
          );
      }
    );
  }


  function applyFaceAppearance(): void {

    faceMeshes.forEach(
      (
        mesh,
        index
      ) => {

        if (
          index ===
          hoveredFaceIndex
        ) {

          mesh.material.color.setHex(
            HOVER_FACE_COLOR
          );

        } else if (
          index ===
          selectedFaceIndex
        ) {

          mesh.material.color.setHex(
            SELECTED_FACE_COLOR
          );

        } else {

          mesh.material.color.setHex(
            DEFAULT_FACE_COLOR
          );
        }
      }
    );
  }


  function setHoveredVertex(
    index: number | null
  ): void {

    const normalizedIndex =
      index !== null
      &&
      index >= 0
      &&
      index <
        vertexNodes.length
        ? index
        : null;

    if (
      hoveredVertexIndex ===
      normalizedIndex
    ) {
      return;
    }

    hoveredVertexIndex =
      normalizedIndex;

    applyVertexAppearance();
  }


  function emitVertexHover(
    index: number | null
  ): void {

    setHoveredVertex(
      index
    );

    if (
      lastViewerHoverIndex ===
      index
    ) {
      return;
    }

    lastViewerHoverIndex =
      index;

    vertexHoverHandler(
      index
    );
  }


  function setVertexHoverHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void {

    vertexHoverHandler =
      handler;
  }


  function setHoveredFace(
    index: number | null
  ): void {

    const normalizedIndex =
      index !== null
      &&
      index >= 0
      &&
      index <
        faceMeshes.length
        ? index
        : null;

    if (
      hoveredFaceIndex ===
      normalizedIndex
    ) {
      return;
    }

    hoveredFaceIndex =
      normalizedIndex;

    applyFaceAppearance();

    applyVertexAppearance();
  }


  function emitFaceHover(
    index: number | null
  ): void {

    setHoveredFace(
      index
    );

    if (
      lastViewerFaceHoverIndex ===
      index
    ) {
      return;
    }

    lastViewerFaceHoverIndex =
      index;

    faceHoverHandler(
      index
    );
  }


  function setSelectedFace(
    index: number | null
  ): void {

    const normalizedIndex =
      index !== null
      &&
      index >= 0
      &&
      index <
        faceMeshes.length
        ? index
        : null;

    if (
      selectedFaceIndex ===
      normalizedIndex
    ) {
      return;
    }

    selectedFaceIndex =
      normalizedIndex;

    applyFaceAppearance();

    applyVertexAppearance();
  }


  function emitFaceSelection(
    index: number | null
  ): void {

    setSelectedFace(
      index
    );

    faceSelectionHandler(
      index
    );
  }


  function setFaceHoverHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void {

    faceHoverHandler =
      handler;
  }


  function setFaceSelectionHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void {

    faceSelectionHandler =
      handler;
  }


  function setFaceSelectMode(
    enabled: boolean
  ): void {

    faceSelectMode =
      enabled;

    if (
      enabled
    ) {

      emitVertexHover(
        null
      );

    } else {

      /*
       * Important V1.0.1 behavior:
       *
       * Clear temporary hover,
       * but DO NOT clear the
       * selected face.
       */
      emitFaceHover(
        null
      );
    }

    renderer.domElement.style.cursor =
      "";
  }


  function clearVertexRoot(): void {

    emitVertexHover(
      null
    );

    vertexNodes.length =
      0;

    vertexLabels.length =
      0;

    vertexLabelObjects.length =
      0;

    if (
      !vertexRoot
    ) {
      return;
    }

    scene.remove(
      vertexRoot
    );

    vertexRoot.traverse(
      (
        object:
        THREE.Object3D
      ) => {

        if (
          object instanceof
          THREE.Mesh
        ) {

          object.geometry.dispose();

          if (
            Array.isArray(
              object.material
            )
          ) {

            object.material.forEach(
              (
                material:
                THREE.Material
              ) => {
                material.dispose();
              }
            );

          } else {

            object.material.dispose();
          }
        }


        if (
          object instanceof
          CSS2DObject
        ) {
          object.element.remove();
        }
      }
    );

    vertexRoot =
      undefined;
  }


  function clear(): void {

    emitVertexHover(
      null
    );

    emitFaceHover(
      null
    );

    selectedFaceIndex =
      null;

    faceMeshes.length =
      0;

    faceVertexIndices.length =
      0;


    if (
      modelRoot
    ) {

      scene.remove(
        modelRoot
      );

      modelRoot.traverse(
        (
          object:
          THREE.Object3D
        ) => {

          if (
            object instanceof
            THREE.Mesh
            ||
            object instanceof
            THREE.LineSegments
          ) {

            object.geometry.dispose();

            if (
              Array.isArray(
                object.material
              )
            ) {

              object.material.forEach(
                (
                  material:
                  THREE.Material
                ) => {
                  material.dispose();
                }
              );

            } else {

              object.material.dispose();
            }
          }
        }
      );

      modelRoot =
        undefined;
    }


    clearVertexRoot();


    if (
      grid
    ) {

      scene.remove(
        grid
      );

      grid.geometry.dispose();

      if (
        Array.isArray(
          grid.material
        )
      ) {

        grid.material.forEach(
          (
            material:
            THREE.Material
          ) => {
            material.dispose();
          }
        );

      } else {

        grid.material.dispose();
      }

      grid =
        undefined;
    }
  }


  function fit(): void {

    if (
      !modelRoot
    ) {
      return;
    }

    const box =
      new THREE.Box3()
        .setFromObject(
          modelRoot
        );

    if (
      box.isEmpty()
    ) {
      return;
    }

    const size =
      new THREE.Vector3();

    const center =
      new THREE.Vector3();

    box.getSize(
      size
    );

    box.getCenter(
      center
    );

    const maxDimension =
      Math.max(
        size.x,
        size.y,
        size.z,
        1
      );

    const fovRadians =
      THREE.MathUtils.degToRad(
        camera.fov
      );

    const distance =
      (
        maxDimension
        /
        (
          2
          *
          Math.tan(
            fovRadians / 2
          )
        )
      )
      *
      1.8;

    camera.position.set(
      center.x
        +
        distance * 0.8,

      center.y
        -
        distance * 0.9,

      center.z
        +
        distance * 0.65
    );

    camera.near =
      Math.max(
        distance / 10000,
        0.1
      );

    camera.far =
      distance * 100;

    camera.updateProjectionMatrix();

    controls.target.copy(
      center
    );

    controls.update();
  }


  function buildVertexNodes(
    parsed: ParsedSmd
  ): void {

    clearVertexRoot();

    const vertices =
      parsed.geometry
        .brep
        .verticesWorld;

    const root =
      new THREE.Group();

    root.name =
      "vertex-debug";

    /*
     * Root stays enabled.
     *
     * Individual nodes/labels
     * control their own visibility.
     */
    root.visible =
      true;

    const modelSize =
      getModelSize(
        vertices
      );

    const radius =
      Math.max(
        modelSize * 0.006,
        8
      );


    for (
      let index = 0;
      index < vertices.length;
      index += 1
    ) {

      const vertex =
        vertices[index];


      const nodeGeometry =
        new THREE.SphereGeometry(
          radius,
          12,
          8
        );

      const nodeMaterial =
        new THREE.MeshBasicMaterial({
          color:
            DEFAULT_VERTEX_COLOR
        });

      const node:
        VertexNode =
        new THREE.Mesh(
          nodeGeometry,
          nodeMaterial
        );

      node.position.set(
        vertex.x,
        vertex.y,
        vertex.z
      );

      node.name =
        `V${index}`;

      node.userData.vertexIndex =
        index;

      root.add(
        node
      );

      vertexNodes.push(
        node
      );


      const labelElement =
        document.createElement(
          "div"
        );

      labelElement.className =
        "vertex-label";

      labelElement.textContent =
        vertexLabelText(
          index,
          vertex
        );

      labelElement.dataset.vertexIndex =
        String(index);

      vertexLabels.push(
        labelElement
      );


      const labelObject =
        new CSS2DObject(
          labelElement
        );

      labelObject.position.set(
        vertex.x,
        vertex.y,
        vertex.z
      );

      labelObject.name =
        `V${index}-label`;

      labelObject.userData.vertexIndex =
        index;

      root.add(
        labelObject
      );

      vertexLabelObjects.push(
        labelObject
      );
    }


    vertexRoot =
      root;

    scene.add(
      root
    );

    applyVertexAppearance();
  }


  function buildFaceMeshes(
    parsed: ParsedSmd,
    root: THREE.Group
  ): void {

    const vertices =
      parsed.geometry
        .brep
        .verticesWorld;

    const faces =
      parsed.geometry
        .brep
        .faces;


    faces.forEach(
      (
        face,
        index
      ) => {

        const geometry =
          buildFaceGeometry(
            vertices,
            face
          );

        const material =
          new THREE.MeshStandardMaterial({
            color:
              DEFAULT_FACE_COLOR,

            roughness:
              0.82,

            metalness:
              0,

            side:
              THREE.DoubleSide
          });

        const mesh:
          FaceNode =
          new THREE.Mesh(
            geometry,
            material
          );

        mesh.name =
          `F${index}`;

        mesh.userData.faceIndex =
          index;

        root.add(
          mesh
        );

        faceMeshes.push(
          mesh
        );

        faceVertexIndices.push(
          [...face.indices]
        );
      }
    );
  }


  function load(
    parsed: ParsedSmd
  ): void {

    clear();

    const root =
      new THREE.Group();


    /*
     * Day 4:
     * each STRAKON face becomes
     * one independent Mesh.
     */
    buildFaceMeshes(
      parsed,
      root
    );


    /*
     * We still build one combined
     * geometry only for the black
     * edge overlay.
     */
    const combinedGeometry =
      buildCombinedGeometry(
        parsed
      );

    const edgeGeometry =
      new THREE.EdgesGeometry(
        combinedGeometry,
        1
      );

    combinedGeometry.dispose();

    const edgeMaterial =
      new THREE.LineBasicMaterial({
        color:
          0x222222
      });

    const edges =
      new THREE.LineSegments(
        edgeGeometry,
        edgeMaterial
      );

    root.add(
      edges
    );


    modelRoot =
      root;

    scene.add(
      root
    );


    buildVertexNodes(
      parsed
    );


    const box =
      new THREE.Box3()
        .setFromObject(
          root
        );

    const size =
      new THREE.Vector3();

    box.getSize(
      size
    );

    grid =
      makeGrid(
        Math.max(
          size.x,
          size.y,
          size.z
        )
        *
        2.5
      );

    scene.add(
      grid
    );

    fit();
  }


  function setVerticesVisible(
    visible: boolean
  ): void {

    verticesVisible =
      visible;

    applyVertexAppearance();

    if (
      !visible
    ) {

      emitVertexHover(
        null
      );

      renderer.domElement.style.cursor =
        "";
    }
  }


  function updatePointer(
    event: PointerEvent
  ): void {

    const rect =
      renderer.domElement
        .getBoundingClientRect();

    pointer.x =
      (
        (
          event.clientX
          -
          rect.left
        )
        /
        rect.width
      )
      *
      2
      -
      1;

    pointer.y =
      -
      (
        (
          event.clientY
          -
          rect.top
        )
        /
        rect.height
      )
      *
      2
      +
      1;
  }


  function raycastFace(
    event: PointerEvent
  ): number | null {

    updatePointer(
      event
    );

    raycaster.setFromCamera(
      pointer,
      camera
    );

    const intersections =
      raycaster.intersectObjects(
        faceMeshes,
        false
      );

    const first =
      intersections[0];

    if (
      !first
    ) {
      return null;
    }

    const index =
      Number(
        first.object
          .userData
          .faceIndex
      );

    return Number.isInteger(
      index
    )
      ? index
      : null;
  }


  function handlePointerMove(
    event: PointerEvent
  ): void {

    /*
     * Face mode has priority.
     */
    if (
      faceSelectMode
    ) {

      emitVertexHover(
        null
      );

      const faceIndex =
        raycastFace(
          event
        );

      emitFaceHover(
        faceIndex
      );

      renderer.domElement.style.cursor =
        faceIndex !== null
          ? "pointer"
          : "";

      return;
    }


    /*
     * Face mode OFF:
     * preserve Day 3 vertex hover.
     */
    emitFaceHover(
      null
    );


    if (
      !verticesVisible
      ||
      vertexNodes.length === 0
    ) {

      emitVertexHover(
        null
      );

      renderer.domElement.style.cursor =
        "";

      return;
    }


    updatePointer(
      event
    );

    raycaster.setFromCamera(
      pointer,
      camera
    );

    const intersections =
      raycaster.intersectObjects(
        vertexNodes,
        false
      );

    const first =
      intersections[0];

    if (
      !first
    ) {

      emitVertexHover(
        null
      );

      renderer.domElement.style.cursor =
        "";

      return;
    }


    const index =
      Number(
        first.object
          .userData
          .vertexIndex
      );

    if (
      !Number.isInteger(
        index
      )
    ) {

      emitVertexHover(
        null
      );

      renderer.domElement.style.cursor =
        "";

      return;
    }


    emitVertexHover(
      index
    );

    renderer.domElement.style.cursor =
      "pointer";
  }


  function handlePointerLeave(): void {

    emitVertexHover(
      null
    );

    emitFaceHover(
      null
    );

    renderer.domElement.style.cursor =
      "";
  }


  function handlePointerClick(
    event: PointerEvent
  ): void {

    /*
     * Clicking the 3D object
     * selects a face only when
     * Face Select mode is ON.
     */
    if (
      !faceSelectMode
    ) {
      return;
    }

    const faceIndex =
      raycastFace(
        event
      );

    if (
      faceIndex === null
    ) {

      emitFaceSelection(
        null
      );

      return;
    }


    /*
     * Clicking selected face
     * again deselects it.
     */
    const nextSelection =
      selectedFaceIndex ===
      faceIndex
        ? null
        : faceIndex;

    emitFaceSelection(
      nextSelection
    );
  }


  renderer.domElement.addEventListener(
    "pointermove",
    handlePointerMove
  );

  renderer.domElement.addEventListener(
    "pointerleave",
    handlePointerLeave
  );

  renderer.domElement.addEventListener(
    "click",
    handlePointerClick
  );


  function resize(): void {

    const width =
      Math.max(
        container.clientWidth,
        1
      );

    const height =
      Math.max(
        container.clientHeight,
        1
      );

    renderer.setSize(
      width,
      height,
      false
    );

    labelRenderer.setSize(
      width,
      height
    );

    camera.aspect =
      width / height;

    camera.updateProjectionMatrix();
  }


  const resizeObserver =
    new ResizeObserver(
      resize
    );

  resizeObserver.observe(
    container
  );

  resize();


  let animationFrame =
    0;


  const animate =
    (): void => {

      animationFrame =
        requestAnimationFrame(
          animate
        );

      controls.update();

      renderer.render(
        scene,
        camera
      );

      labelRenderer.render(
        scene,
        camera
      );
    };


  animate();


  return {

    load,

    clear,

    fit,

    setVerticesVisible,

    setHoveredVertex,

    setVertexHoverHandler,

    setFaceSelectMode,

    setHoveredFace,

    setSelectedFace,

    setFaceHoverHandler,

    setFaceSelectionHandler,


    dispose(): void {

      cancelAnimationFrame(
        animationFrame
      );

      resizeObserver.disconnect();

      renderer.domElement.removeEventListener(
        "pointermove",
        handlePointerMove
      );

      renderer.domElement.removeEventListener(
        "pointerleave",
        handlePointerLeave
      );

      renderer.domElement.removeEventListener(
        "click",
        handlePointerClick
      );

      clear();

      controls.dispose();

      renderer.dispose();

      renderer.domElement.remove();

      labelRenderer.domElement.remove();
    }
  };
}


export function boundingBoxText(
  vertices: Vec3[]
): string {

  if (
    vertices.length === 0
  ) {
    return "-";
  }

  let minX = Infinity;
  let minY = Infinity;
  let minZ = Infinity;

  let maxX = -Infinity;
  let maxY = -Infinity;
  let maxZ = -Infinity;

  for (
    const vertex of vertices
  ) {

    minX =
      Math.min(
        minX,
        vertex.x
      );

    minY =
      Math.min(
        minY,
        vertex.y
      );

    minZ =
      Math.min(
        minZ,
        vertex.z
      );

    maxX =
      Math.max(
        maxX,
        vertex.x
      );

    maxY =
      Math.max(
        maxY,
        vertex.y
      );

    maxZ =
      Math.max(
        maxZ,
        vertex.z
      );
  }

  return (
    `X ${minX}..${maxX}` +
    ` | Y ${minY}..${maxY}` +
    ` | Z ${minZ}..${maxZ} mm`
  );
}