import * as THREE from "three";

import {
  OrbitControls
} from
  "three/examples/jsm/controls/OrbitControls.js";

import {
  CSS2DObject,
  CSS2DRenderer
} from
  "three/examples/jsm/renderers/CSS2DRenderer.js";

import type {
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

  dispose(): void;
}


/**
 * Format a coordinate for display.
 *
 * Integer-like values:
 * 375
 * -325
 * 5000
 *
 * Decimal values:
 * 1847.804
 */
function formatCoordinate(
  value: number
): string {

  // Avoid displaying "-0".
  if (
    Math.abs(value) < 1e-9
  ) {
    value = 0;
  }


  const rounded =
    Math.round(value);


  if (
    Math.abs(
      value - rounded
    ) < 1e-6
  ) {

    return String(
      rounded
    );

  }


  return value.toFixed(3);
}


/**
 * Build the text displayed beside one vertex.
 *
 * Example:
 *
 * V8 (375, -325, 1847.804)
 */
function vertexLabelText(
  index: number,
  vertex: Vec3
): string {

  return (
    `V${index} (` +
    `${formatCoordinate(vertex.x)}, ` +
    `${formatCoordinate(vertex.y)}, ` +
    `${formatCoordinate(vertex.z)}` +
    `)`
  );
}


function buildGeometry(
  parsed: ParsedSmd
): THREE.BufferGeometry {

  const vertices =
    parsed.geometry.brep.verticesWorld;

  const positions: number[] = [];


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
        const vertexIndex
        of triangle
      ) {

        const v =
          vertices[vertexIndex];

        positions.push(
          v.x,
          v.y,
          v.z
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


function makeGrid(
  size: number
): THREE.GridHelper {

  const safeSize =
    Math.max(
      size,
      1000
    );

  const divisions = 20;


  const grid =
    new THREE.GridHelper(
      safeSize,
      divisions
    );


  // Three.js GridHelper is created in XZ.
  // STRAKON uses Z as vertical,
  // therefore rotate to the XY plane.

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


  // -------------------------------------------------
  // Main WebGL renderer
  // -------------------------------------------------

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


  // -------------------------------------------------
  // CSS label renderer
  //
  // WebGLRenderer:
  //   solid / edges / nodes
  //
  // CSS2DRenderer:
  //   V0 (x,y,z) text
  // -------------------------------------------------

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


  controls.enableDamping = true;

  controls.dampingFactor = 0.08;


  const ambient =
    new THREE.AmbientLight(
      0xffffff,
      1.8
    );


  scene.add(ambient);


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


  scene.add(keyLight);


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


  scene.add(fillLight);


  const axes =
    new THREE.AxesHelper(
      1000
    );


  scene.add(axes);


  let modelRoot:
    THREE.Group | undefined;


  let vertexRoot:
    THREE.Group | undefined;


  let grid:
    THREE.GridHelper | undefined;


  let verticesVisible = false;


  /**
   * Remove the current vertex debug group.
   *
   * This includes:
   *
   * - blue node meshes
   * - CSS2D coordinate labels
   */
  function clearVertexRoot(): void {

    if (!vertexRoot) {
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

        // Dispose the sphere nodes.

        if (
          object
          instanceof THREE.Mesh
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
              ) =>
                material.dispose()
            );

          } else {

            object.material.dispose();

          }

        }


        // Remove any HTML labels.

        if (
          object
          instanceof CSS2DObject
        ) {

          object.element.remove();

        }

      }
    );


    vertexRoot = undefined;
  }


  function clear(): void {

    if (modelRoot) {

      scene.remove(
        modelRoot
      );


      modelRoot.traverse(
        (
          object:
          THREE.Object3D
        ) => {

          if (
            object
              instanceof THREE.Mesh
            ||
            object
              instanceof THREE.LineSegments
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
                ) =>
                  material.dispose()
              );

            } else {

              object.material.dispose();

            }

          }

        }
      );


      modelRoot = undefined;
    }


    clearVertexRoot();


    if (grid) {

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
          ) =>
            material.dispose()
        );

      } else {

        grid.material.dispose();

      }


      grid = undefined;
    }

  }


  function fit(): void {

    if (!modelRoot) {
      return;
    }


    const box =
      new THREE.Box3()
        .setFromObject(
          modelRoot
        );


    if (box.isEmpty()) {
      return;
    }


    const size =
      new THREE.Vector3();


    const center =
      new THREE.Vector3();


    box.getSize(size);

    box.getCenter(center);


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
        + distance * 0.8,

      center.y
        - distance * 0.9,

      center.z
        + distance * 0.65

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


  /**
   * Build:
   *
   * V0 dot
   * V0 (x,y,z) label
   *
   * V1 dot
   * V1 (x,y,z) label
   *
   * ...
   *
   * IMPORTANT:
   * verticesWorld is used.
   *
   * This means BREP transforms have already
   * been applied.
   */
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


    root.visible =
      verticesVisible;


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


      // ---------------------------------------------
      // Node / dot
      // ---------------------------------------------

      const geometry =
        new THREE.SphereGeometry(
          radius,
          12,
          8
        );


      const material =
        new THREE.MeshBasicMaterial({
          color: 0x2563eb
        });


      const node =
        new THREE.Mesh(
          geometry,
          material
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


      root.add(node);


      // ---------------------------------------------
      // Coordinate label
      // ---------------------------------------------

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

    }


    vertexRoot =
      root;


    scene.add(
      root
    );
  }


  function load(
    parsed: ParsedSmd
  ): void {

    clear();


    const geometry =
      buildGeometry(parsed);


    const root =
      new THREE.Group();


    const material =
      new THREE.MeshStandardMaterial({

        color: 0xc9cdd2,

        roughness: 0.82,

        metalness: 0,

        side:
          THREE.DoubleSide

      });


    const mesh =
      new THREE.Mesh(
        geometry,
        material
      );


    root.add(
      mesh
    );


    const edgeGeometry =
      new THREE.EdgesGeometry(
        geometry,
        1
      );


    const edgeMaterial =
      new THREE.LineBasicMaterial({
        color: 0x222222
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


    // Build all BREP vertex dots and labels.
    // They remain hidden until the toolbar
    // button enables them.

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


    if (
      vertexRoot
    ) {

      vertexRoot.visible =
        visible;

    }

  }


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


  let animationFrame = 0;


  const animate = (): void => {

    animationFrame =
      requestAnimationFrame(
        animate
      );


    controls.update();


    // Render geometry.
    renderer.render(
      scene,
      camera
    );


    // Render coordinate labels.
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


    dispose(): void {

      cancelAnimationFrame(
        animationFrame
      );


      resizeObserver.disconnect();


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
    const v of vertices
  ) {

    minX =
      Math.min(
        minX,
        v.x
      );

    minY =
      Math.min(
        minY,
        v.y
      );

    minZ =
      Math.min(
        minZ,
        v.z
      );


    maxX =
      Math.max(
        maxX,
        v.x
      );

    maxY =
      Math.max(
        maxY,
        v.y
      );

    maxZ =
      Math.max(
        maxZ,
        v.z
      );

  }


  return (
    `X ${minX}..${maxX}` +
    ` | Y ${minY}..${maxY}` +
    ` | Z ${minZ}..${maxZ} mm`
  );
}