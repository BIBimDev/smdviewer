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


  /*
   * =================================================
   * Vertex API
   * =================================================
   */

  setVerticesVisible(
    visible: boolean
  ): void;


  setHoveredVertex(
    index: number | null
  ): void;


  setSelectedVertex(
    index: number | null
  ): void;


  setVertexHoverHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void;


  setVertexSelectionHandler(
    handler:
      (
        index: number | null
      ) => void
  ): void;


  /*
   * =================================================
   * Face API
   * =================================================
   */

  setFaceSelectMode(
    enabled: boolean
  ): void;


  setFaceColorsEnabled(
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


/*
 * =================================================
 * Vertex colors
 * =================================================
 */

const DEFAULT_VERTEX_COLOR =
  0x2563eb;


const HOVER_VERTEX_COLOR =
  0xf59e0b;


const SELECTED_VERTEX_COLOR =
  0x7c3aed;


const MUTED_VERTEX_COLOR =
  0x94a3b8;


const FACE_VERTEX_COLOR =
  0xdc2626;


/*
 * =================================================
 * Face colors
 * =================================================
 */

const DEFAULT_FACE_COLOR =
  0xc9cdd2;


const HOVER_FACE_COLOR =
  0xfbbf24;


const SELECTED_FACE_COLOR =
  0x60a5fa;


const FACE_COLOR_PALETTE = [

  0x90caf9,

  0xa5d6a7,

  0xffcc80,

  0xce93d8,

  0x80cbc4,

  0xef9a9a,

  0xfff59d,

  0xb0bec5,

  0x9fa8da,

  0xbcaaa4,

  0x80deea,

  0xc5e1a5

];


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


/*
 * =================================================
 * Coordinate formatting
 * =================================================
 */

function formatCoordinate(
  value: number
): string {

  const normalized =

    Math.abs(value) < 1e-9

      ? 0

      : value;


  const rounded =
    Math.round(
      normalized
    );


  if (
    Math.abs(
      normalized - rounded
    ) < 1e-6
  ) {

    return String(
      rounded
    );
  }


  return normalized.toFixed(
    3
  );
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


/*
 * =================================================
 * Geometry construction
 * =================================================
 */

function buildCombinedGeometry(
  parsed: ParsedSmd
): THREE.BufferGeometry {

  const vertices =
    parsed
      .geometry
      .brep
      .verticesWorld;


  const positions:
    number[] =
    [];


  for (
    const face of
    parsed
      .geometry
      .brep
      .faces
  ) {

    const triangles =
      triangulateFace(
        vertices,
        face
      );


    for (
      const triangle of
      triangles
    ) {

      for (
        const vertexIndex of
        triangle
      ) {

        const vertex =
          vertices[
            vertexIndex
          ];


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

  const positions:
    number[] =
    [];


  const triangles =
    triangulateFace(
      vertices,
      face
    );


  for (
    const triangle of
    triangles
  ) {

    for (
      const vertexIndex of
      triangle
    ) {

      const vertex =
        vertices[
          vertexIndex
        ];


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


/*
 * =================================================
 * Grid
 * =================================================
 */

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


  /*
   * GridHelper starts on XZ.
   *
   * STRAKON is Z-up, therefore
   * rotate grid onto XY.
   */
  grid.rotation.x =
    Math.PI / 2;


  return grid;
}


/*
 * =================================================
 * Model size
 * =================================================
 */

function getModelSize(
  vertices: Vec3[]
): number {

  if (
    vertices.length === 0
  ) {

    return 1000;
  }


  let minX =
    Infinity;

  let minY =
    Infinity;

  let minZ =
    Infinity;


  let maxX =
    -Infinity;

  let maxY =
    -Infinity;

  let maxZ =
    -Infinity;


  for (
    const vertex of
    vertices
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


/*
 * =================================================
 * Viewer
 * =================================================
 */

export function createViewer(
  container: HTMLElement
): ViewerHandle {

  THREE.Object3D.DEFAULT_UP.set(
    0,
    0,
    1
  );


  /*
   * =================================================
   * Scene
   * =================================================
   */

  const scene =
    new THREE.Scene();


  scene.background =
    new THREE.Color(
      0xf4f6f8
    );


  /*
   * =================================================
   * Camera
   * =================================================
   */

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


  /*
   * =================================================
   * WebGL renderer
   * =================================================
   */

  const renderer =
    new THREE.WebGLRenderer({

      antialias:
        true

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


  /*
   * =================================================
   * CSS2D renderer
   * =================================================
   */

  const labelRenderer =
    new CSS2DRenderer();


  labelRenderer
    .domElement
    .className =
    "vertex-label-layer";


  labelRenderer
    .domElement
    .style
    .position =
    "absolute";


  labelRenderer
    .domElement
    .style
    .left =
    "0";


  labelRenderer
    .domElement
    .style
    .top =
    "0";


  /*
   * The overlay itself must not block
   * Three.js interaction.
   *
   * Day 3 CSS enables pointer events
   * only on individual .vertex-label
   * elements.
   */
  labelRenderer
    .domElement
    .style
    .pointerEvents =
    "none";


  container.appendChild(
    labelRenderer.domElement
  );


  /*
   * =================================================
   * Orbit controls
   * =================================================
   */

  const controls =
    new OrbitControls(

      camera,

      renderer.domElement

    );


  controls.enableDamping =
    true;


  controls.dampingFactor =
    0.08;


  /*
   * =================================================
   * Lighting
   * =================================================
   */

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


  /*
   * =================================================
   * Axes
   * =================================================
   */

  const axes =
    new THREE.AxesHelper(
      1000
    );


  scene.add(
    axes
  );


  /*
   * =================================================
   * Raycasting
   * =================================================
   */

  const raycaster =
    new THREE.Raycaster();


  const pointer =
    new THREE.Vector2();


  /*
   * =================================================
   * Scene objects
   * =================================================
   */

  let modelRoot:
    THREE.Group | undefined;


  let vertexRoot:
    THREE.Group | undefined;


  let grid:
    THREE.GridHelper | undefined;


  /*
   * =================================================
   * Mode state
   * =================================================
   */

  let verticesVisible =
    false;


  let faceSelectMode =
    false;


  let faceColorsEnabled =
    false;


  /*
   * =================================================
   * Vertex interaction state
   * =================================================
   */

  let hoveredVertexIndex:
    number | null =
    null;


  let selectedVertexIndex:
    number | null =
    null;


  /*
   * =================================================
   * Face interaction state
   * =================================================
   */

  let hoveredFaceIndex:
    number | null =
    null;


  let selectedFaceIndex:
    number | null =
    null;


  /*
   * Last state emitted from the
   * 3D/label viewer back to main.ts.
   */
  let lastViewerHoverIndex:
    number | null =
    null;


  let lastViewerFaceHoverIndex:
    number | null =
    null;


  /*
   * =================================================
   * Callbacks
   * =================================================
   */

  let vertexHoverHandler:

    (
      index:
        number | null
    ) => void

    =
    () => undefined;


  let vertexSelectionHandler:

    (
      index:
        number | null
    ) => void

    =
    () => undefined;


  let faceHoverHandler:

    (
      index:
        number | null
    ) => void

    =
    () => undefined;


  let faceSelectionHandler:

    (
      index:
        number | null
    ) => void

    =
    () => undefined;


  /*
   * =================================================
   * Vertex / face object arrays
   * =================================================
   */

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


  /*
   * =================================================
   * Day 3 label interaction mode
   * =================================================
   */

  function updateLabelInteractionMode(): void {

    /*
     * Face Select has priority.
     *
     * When active, labels become
     * transparent to pointer events.
     */
    labelRenderer
      .domElement
      .classList
      .toggle(

        "face-selection-active",

        faceSelectMode

      );


    /*
     * Pointer cursor / persistent
     * selection is enabled only when:
     *
     * Show Vertices = ON
     * Face Select = OFF
     */
    labelRenderer
      .domElement
      .classList
      .toggle(

        "vertex-selection-enabled",

        verticesVisible

        &&

        !faceSelectMode

      );
  }


  /*
   * =================================================
   * Active face vertices
   * =================================================
   */

  function activeFaceVertexSet():
    Set<number> {

    const activeFace =

      hoveredFaceIndex !==
      null

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


  /*
   * =================================================
   * Vertex appearance
   * =================================================
   */

  function applyVertexAppearance(): void {

    const faceVertices =
      activeFaceVertexSet();


    /*
     * Muting occurs only when:
     *
     * Show Vertices = ON
     * and one vertex is selected.
     */
    const selectionActive =

      verticesVisible

      &&

      selectedVertexIndex !==
        null;


    vertexNodes.forEach(
      (
        node,
        index
      ) => {

        const isHovered =

          index ===
          hoveredVertexIndex;


        const isSelected =

          index ===
          selectedVertexIndex;


        const isFaceVertex =

          faceVertices.has(
            index
          );


        /*
         * A vertex is visible when:
         *
         * 1. Show Vertices is ON
         * 2. temporarily hovered
         * 3. persistently selected
         * 4. referenced by active face
         */
        node.visible =

          verticesVisible

          ||

          isHovered

          ||

          isSelected

          ||

          isFaceVertex;


        /*
         * Appearance priority:
         *
         * hover
         * >
         * selected
         * >
         * face-related
         * >
         * muted
         * >
         * normal
         */

        if (
          isHovered
        ) {

          node
            .material
            .color
            .setHex(
              HOVER_VERTEX_COLOR
            );


          node.scale.setScalar(
            2.5
          );


        } else if (
          isSelected
        ) {

          node
            .material
            .color
            .setHex(
              SELECTED_VERTEX_COLOR
            );


          node.scale.setScalar(
            2.2
          );


        } else if (
          isFaceVertex
        ) {

          node
            .material
            .color
            .setHex(
              FACE_VERTEX_COLOR
            );


          node.scale.setScalar(
            1.8
          );


        } else if (
          selectionActive
        ) {

          node
            .material
            .color
            .setHex(
              MUTED_VERTEX_COLOR
            );


          node.scale.setScalar(
            0.85
          );


        } else {

          node
            .material
            .color
            .setHex(
              DEFAULT_VERTEX_COLOR
            );


          node.scale.setScalar(
            1
          );
        }
      }
    );


    /*
     * HTML label classes.
     */
    vertexLabels.forEach(
      (
        label,
        index
      ) => {

        const isHovered =

          index ===
          hoveredVertexIndex;


        const isSelected =

          index ===
          selectedVertexIndex;


        const isFaceVertex =

          faceVertices.has(
            index
          );


        const isMuted =

          selectionActive

          &&

          !isSelected

          &&

          !isHovered

          &&

          !isFaceVertex;


        label.classList.toggle(

          "is-hovered",

          isHovered

        );


        label.classList.toggle(

          "is-selected",

          isSelected

        );


        label.classList.toggle(

          "is-face-related",

          isFaceVertex

        );


        label.classList.toggle(

          "is-muted",

          isMuted

        );
      }
    );


    /*
     * CSS2D object visibility.
     */
    vertexLabelObjects.forEach(
      (
        labelObject,
        index
      ) => {

        labelObject.visible =

          verticesVisible

          ||

          index ===
            hoveredVertexIndex

          ||

          index ===
            selectedVertexIndex

          ||

          faceVertices.has(
            index
          );
      }
    );
  }


  /*
   * =================================================
   * Face appearance
   * =================================================
   */

  function baseFaceColor(
    faceIndex: number
  ): number {

    if (
      !faceColorsEnabled
    ) {

      return DEFAULT_FACE_COLOR;
    }


    return FACE_COLOR_PALETTE[

      faceIndex

      %

      FACE_COLOR_PALETTE.length

    ];
  }


  function applyFaceAppearance(): void {

    faceMeshes.forEach(
      (
        mesh,
        index
      ) => {

        /*
         * Priority:
         *
         * hover
         * >
         * selection
         * >
         * face palette
         * >
         * gray
         */

        if (
          index ===
          hoveredFaceIndex
        ) {

          mesh
            .material
            .color
            .setHex(
              HOVER_FACE_COLOR
            );


          return;
        }


        if (
          index ===
          selectedFaceIndex
        ) {

          mesh
            .material
            .color
            .setHex(
              SELECTED_FACE_COLOR
            );


          return;
        }


        mesh
          .material
          .color
          .setHex(

            baseFaceColor(
              index
            )

          );
      }
    );
  }


  /*
   * =================================================
   * Vertex hover
   * =================================================
   */

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

    /*
     * Update visual state first.
     */
    setHoveredVertex(
      index
    );


    /*
     * Avoid continuously emitting the
     * same hover callback.
     */
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
        index:
          number | null
      ) => void

  ): void {

    vertexHoverHandler =
      handler;
  }


  /*
   * =================================================
   * Vertex selection
   * =================================================
   */

  function setSelectedVertex(
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
      selectedVertexIndex ===
      normalizedIndex
    ) {

      return;
    }


    selectedVertexIndex =
      normalizedIndex;


    applyVertexAppearance();
  }


  function emitVertexSelection(
    index: number | null
  ): void {

    setSelectedVertex(
      index
    );


    /*
     * 3D / label
     *     ↓
     * main.ts table
     */
    vertexSelectionHandler(
      index
    );
  }


  function setVertexSelectionHandler(

    handler:
      (
        index:
          number | null
      ) => void

  ): void {

    vertexSelectionHandler =
      handler;
  }


  /*
   * =================================================
   * Face hover
   * =================================================
   */

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


  function setFaceHoverHandler(

    handler:
      (
        index:
          number | null
      ) => void

  ): void {

    faceHoverHandler =
      handler;
  }


  /*
   * =================================================
   * Face selection
   * =================================================
   */

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


  function setFaceSelectionHandler(

    handler:
      (
        index:
          number | null
      ) => void

  ): void {

    faceSelectionHandler =
      handler;
  }


  /*
   * =================================================
   * Face Select mode
   * V1.0.2 Day 1 + Day 3
   * =================================================
   */

  function setFaceSelectMode(
    enabled: boolean
  ): void {

    faceSelectMode =
      enabled;


    /*
     * Day 3:
     *
     * Face Select ON makes the
     * coordinate labels transparent
     * to mouse interaction.
     */
    updateLabelInteractionMode();


    if (
      enabled
    ) {

      /*
       * Face picking takes priority
       * over vertex hover.
       */
      emitVertexHover(
        null
      );


    } else {

      /*
       * V1.0.2 Day 1:
       *
       * Leaving Face Select mode
       * clears all persistent and
       * temporary face state.
       */
      emitFaceHover(
        null
      );


      emitFaceSelection(
        null
      );
    }


    renderer
      .domElement
      .style
      .cursor =
      "";
  }


  /*
   * =================================================
   * Face Colors mode
   * =================================================
   */

  function setFaceColorsEnabled(
    enabled: boolean
  ): void {

    faceColorsEnabled =
      enabled;


    applyFaceAppearance();
  }


  /*
   * =================================================
   * Clear vertices
   * =================================================
   */

  function clearVertexRoot(): void {

    emitVertexHover(
      null
    );


    emitVertexSelection(
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

          object
            .geometry
            .dispose();


          if (
            Array.isArray(
              object.material
            )
          ) {

            object
              .material
              .forEach(
                (
                  material:
                    THREE.Material
                ) => {

                  material.dispose();

                }
              );


          } else {

            object
              .material
              .dispose();
          }
        }


        if (
          object instanceof
          CSS2DObject
        ) {

          object
            .element
            .remove();
        }
      }
    );


    vertexRoot =
      undefined;
  }


  /*
   * =================================================
   * Clear complete model
   * =================================================
   */

  function clear(): void {

    emitVertexHover(
      null
    );


    emitVertexSelection(
      null
    );


    emitFaceHover(
      null
    );


    emitFaceSelection(
      null
    );


    faceMeshes.length =
      0;


    faceVertexIndices.length =
      0;


    selectedFaceIndex =
      null;


    hoveredFaceIndex =
      null;


    selectedVertexIndex =
      null;


    hoveredVertexIndex =
      null;


    /*
     * Remove model faces + edges.
     */
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

            object
              .geometry
              .dispose();


            if (
              Array.isArray(
                object.material
              )
            ) {

              object
                .material
                .forEach(
                  (
                    material:
                      THREE.Material
                  ) => {

                    material.dispose();

                  }
                );


            } else {

              object
                .material
                .dispose();
            }
          }
        }
      );


      modelRoot =
        undefined;
    }


    clearVertexRoot();


    /*
     * Remove grid.
     */
    if (
      grid
    ) {

      scene.remove(
        grid
      );


      grid
        .geometry
        .dispose();


      if (
        Array.isArray(
          grid.material
        )
      ) {

        grid
          .material
          .forEach(
            (
              material:
                THREE.Material
            ) => {

              material.dispose();

            }
          );


      } else {

        grid
          .material
          .dispose();
      }


      grid =
        undefined;
    }
  }


  /*
   * =================================================
   * Camera fit
   * =================================================
   */

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


  /*
   * =================================================
   * Build vertex nodes and labels
   * =================================================
   */

  function buildVertexNodes(
    parsed: ParsedSmd
  ): void {

    clearVertexRoot();


    const vertices =
      parsed
        .geometry
        .brep
        .verticesWorld;


    const root =
      new THREE.Group();


    root.name =
      "vertex-debug";


    /*
     * Root remains visible.
     *
     * Individual vertices control
     * their own visibility.
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

      index <
        vertices.length;

      index += 1
    ) {

      const vertex =
        vertices[
          index
        ];


      /*
       * ---------------------------------------------
       * Vertex sphere
       * ---------------------------------------------
       */

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


      node
        .userData
        .vertexIndex =
        index;


      root.add(
        node
      );


      vertexNodes.push(
        node
      );


      /*
       * ---------------------------------------------
       * Coordinate label
       * ---------------------------------------------
       */

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


      labelElement
        .dataset
        .vertexIndex =
        String(
          index
        );


      /*
       * =================================================
       * V1.0.2 Day 3
       * LABEL -> DOT + TABLE hover
       * =================================================
       */

      labelElement.addEventListener(
        "pointerenter",
        () => {

          /*
           * Face Select owns pointer
           * interaction while active.
           */
          if (
            faceSelectMode
          ) {

            return;
          }


          emitVertexHover(
            index
          );
        }
      );


      labelElement.addEventListener(
        "pointerleave",
        () => {

          if (
            faceSelectMode
          ) {

            return;
          }


          /*
           * Clear temporary hover only.
           *
           * Persistent selection remains.
           */
          emitVertexHover(
            null
          );
        }
      );


      /*
       * =================================================
       * V1.0.2 Day 3
       * LABEL -> persistent selection
       * =================================================
       */

      labelElement.addEventListener(
        "click",
        (
          event
        ) => {

          /*
           * Keep this click within the
           * label interaction.
           */
          event.stopPropagation();


          /*
           * Face Select has priority.
           */
          if (
            faceSelectMode
          ) {

            return;
          }


          /*
           * Hidden-vertex mode remains
           * inspection only.
           */
          if (
            !verticesVisible
          ) {

            return;
          }


          const nextSelection =

            selectedVertexIndex ===
            index

              ? null

              : index;


          emitVertexSelection(
            nextSelection
          );
        }
      );


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


      labelObject
        .userData
        .vertexIndex =
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


  /*
   * =================================================
   * Build individual face meshes
   * =================================================
   */

  function buildFaceMeshes(
    parsed: ParsedSmd,
    root: THREE.Group
  ): void {

    const vertices =
      parsed
        .geometry
        .brep
        .verticesWorld;


    const faces =
      parsed
        .geometry
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
              baseFaceColor(
                index
              ),

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


        mesh
          .userData
          .faceIndex =
          index;


        root.add(
          mesh
        );


        faceMeshes.push(
          mesh
        );


        faceVertexIndices.push(
          [
            ...face.indices
          ]
        );
      }
    );
  }


  /*
   * =================================================
   * Load geometry
   * =================================================
   */

  function load(
    parsed: ParsedSmd
  ): void {

    clear();


    const root =
      new THREE.Group();


    /*
     * Individual BREP face meshes.
     */
    buildFaceMeshes(
      parsed,
      root
    );


    /*
     * Separate combined geometry
     * only for edge rendering.
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


    /*
     * Vertex nodes and labels.
     */
    buildVertexNodes(
      parsed
    );


    /*
     * Build grid from model size.
     */
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


    /*
     * Reapply current UI modes.
     */
    applyFaceAppearance();

    applyVertexAppearance();

    updateLabelInteractionMode();
  }


  /*
   * =================================================
   * Show / Hide vertices
   * V1.0.2 Day 2 + Day 3
   * =================================================
   */

  function setVerticesVisible(
    visible: boolean
  ): void {

    verticesVisible =
      visible;


    /*
     * Day 3:
     * update label click/hover mode.
     */
    updateLabelInteractionMode();


    /*
     * Turning vertices OFF clears:
     *
     * - persistent vertex selection
     * - current vertex hover
     *
     * A subsequent table hover can
     * temporarily display a vertex.
     */
    if (
      !visible
    ) {

      emitVertexHover(
        null
      );


      emitVertexSelection(
        null
      );


      renderer
        .domElement
        .style
        .cursor =
        "";
    }


    applyVertexAppearance();
  }


  /*
   * =================================================
   * Pointer coordinates
   * =================================================
   */

  function updatePointer(
    event: PointerEvent
  ): void {

    const rect =
      renderer
        .domElement
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


  /*
   * =================================================
   * Vertex raycast
   * =================================================
   */

  function raycastVertex(
    event: PointerEvent
  ): number | null {

    updatePointer(
      event
    );


    raycaster.setFromCamera(
      pointer,
      camera
    );


    /*
     * Only visible nodes should be
     * eligible for picking.
     */
    const visibleNodes =
      vertexNodes.filter(
        (
          node
        ) =>
          node.visible
      );


    const intersections =
      raycaster.intersectObjects(

        visibleNodes,

        false

      );


    const first =
      intersections[
        0
      ];


    if (
      !first
    ) {

      return null;
    }


    const index =
      Number(

        first
          .object
          .userData
          .vertexIndex

      );


    return Number.isInteger(
      index
    )

      ? index

      : null;
  }


  /*
   * =================================================
   * Face raycast
   * =================================================
   */

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
      intersections[
        0
      ];


    if (
      !first
    ) {

      return null;
    }


    const index =
      Number(

        first
          .object
          .userData
          .faceIndex

      );


    return Number.isInteger(
      index
    )

      ? index

      : null;
  }


  /*
   * =================================================
   * Pointer move
   * =================================================
   */

  function handlePointerMove(
    event: PointerEvent
  ): void {

    /*
     * ---------------------------------------------
     * FACE MODE
     * ---------------------------------------------
     *
     * Face Select has priority.
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


      renderer
        .domElement
        .style
        .cursor =

        faceIndex !== null

          ? "pointer"

          : "";


      return;
    }


    /*
     * ---------------------------------------------
     * FACE MODE OFF
     * ---------------------------------------------
     */

    emitFaceHover(
      null
    );


    /*
     * ---------------------------------------------
     * VERTEX MODE
     * ---------------------------------------------
     *
     * Direct 3D vertex hover is available
     * only when Show Vertices is ON.
     *
     * When OFF, temporary vertex inspection
     * originates from the vertex table.
     */
    if (
      !verticesVisible

      ||

      vertexNodes.length === 0
    ) {

      emitVertexHover(
        null
      );


      renderer
        .domElement
        .style
        .cursor =
        "";


      return;
    }


    const vertexIndex =
      raycastVertex(
        event
      );


    emitVertexHover(
      vertexIndex
    );


    renderer
      .domElement
      .style
      .cursor =

      vertexIndex !== null

        ? "pointer"

        : "";
  }


  /*
   * =================================================
   * Pointer leave
   * =================================================
   */

  function handlePointerLeave(): void {

    emitVertexHover(
      null
    );


    emitFaceHover(
      null
    );


    renderer
      .domElement
      .style
      .cursor =
      "";
  }


  /*
   * =================================================
   * Pointer click
   * =================================================
   */

  function handlePointerClick(
    event: PointerEvent
  ): void {

    /*
     * ---------------------------------------------
     * FACE SELECTION
     * ---------------------------------------------
     *
     * Face Select mode has priority.
     */
    if (
      faceSelectMode
    ) {

      const faceIndex =
        raycastFace(
          event
        );


      /*
       * Empty 3D space clears the
       * selected face.
       */
      if (
        faceIndex === null
      ) {

        emitFaceSelection(
          null
        );


        return;
      }


      /*
       * Clicking selected face again
       * deselects it.
       */
      const nextFaceSelection =

        selectedFaceIndex ===
        faceIndex

          ? null

          : faceIndex;


      emitFaceSelection(
        nextFaceSelection
      );


      return;
    }


    /*
     * ---------------------------------------------
     * VERTEX SELECTION
     * ---------------------------------------------
     *
     * Persistent selection is available
     * only while Show Vertices is ON.
     */
    if (
      !verticesVisible
    ) {

      return;
    }


    const vertexIndex =
      raycastVertex(
        event
      );


    /*
     * Clicking anywhere except a
     * vertex clears current selection.
     */
    if (
      vertexIndex === null
    ) {

      emitVertexSelection(
        null
      );


      return;
    }


    /*
     * Clicking selected vertex again
     * deselects it.
     */
    const nextVertexSelection =

      selectedVertexIndex ===
      vertexIndex

        ? null

        : vertexIndex;


    emitVertexSelection(
      nextVertexSelection
    );
  }


  /*
   * =================================================
   * Canvas listeners
   * =================================================
   */

  renderer
    .domElement
    .addEventListener(

      "pointermove",

      handlePointerMove

    );


  renderer
    .domElement
    .addEventListener(

      "pointerleave",

      handlePointerLeave

    );


  renderer
    .domElement
    .addEventListener(

      "click",

      handlePointerClick

    );


  /*
   * =================================================
   * Resize
   * =================================================
   */

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


  /*
   * =================================================
   * Animation
   * =================================================
   */

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


  /*
   * Establish initial CSS2D
   * interaction state.
   */
  updateLabelInteractionMode();


  /*
   * =================================================
   * Public API
   * =================================================
   */

  return {

    load,

    clear,

    fit,


    /*
     * Vertices
     */
    setVerticesVisible,

    setHoveredVertex,

    setSelectedVertex,

    setVertexHoverHandler,

    setVertexSelectionHandler,


    /*
     * Faces
     */
    setFaceSelectMode,

    setFaceColorsEnabled,

    setHoveredFace,

    setSelectedFace,

    setFaceHoverHandler,

    setFaceSelectionHandler,


    /*
     * Cleanup
     */
    dispose(): void {

      cancelAnimationFrame(
        animationFrame
      );


      resizeObserver.disconnect();


      renderer
        .domElement
        .removeEventListener(

          "pointermove",

          handlePointerMove

        );


      renderer
        .domElement
        .removeEventListener(

          "pointerleave",

          handlePointerLeave

        );


      renderer
        .domElement
        .removeEventListener(

          "click",

          handlePointerClick

        );


      clear();


      controls.dispose();


      renderer.dispose();


      renderer
        .domElement
        .remove();


      labelRenderer
        .domElement
        .remove();
    }
  };
}


/*
 * =================================================
 * Bounding-box helper
 * =================================================
 */

export function boundingBoxText(
  vertices: Vec3[]
): string {

  if (
    vertices.length === 0
  ) {

    return "-";
  }


  let minX =
    Infinity;

  let minY =
    Infinity;

  let minZ =
    Infinity;


  let maxX =
    -Infinity;

  let maxY =
    -Infinity;

  let maxZ =
    -Infinity;


  for (
    const vertex of
    vertices
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