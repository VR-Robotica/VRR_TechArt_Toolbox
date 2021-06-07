using System.IO;
using System.Collections.Generic;

using UnityEditor;
using UnityEditor.Presets;

using UnityEditor.PackageManager.Requests;
using UnityEditor.PackageManager;
using UnityEngine;

using VRR.TechArtTools.Menu;
using VRR.TechArtTools.Lightmapping;
using VRR.TechArtTools.Utilities;

using VRR.Integrations;

#if UNITY_FBX_EXPORT
using UnityEditor.Formats.Fbx.Exporter;
#endif


namespace VRR.TechArtTools.MeshTools
{
    public class MenuTab_MeshTools : MenuTab<Tools_Mesh>
    {
        SerializedProperty WillModifyLightmapUVs;
        SerializedProperty LightmapUVSet;
        SerializedProperty SourceGroup;
        SerializedProperty SourceObjects;
        SerializedProperty Prefix;

        SerializedProperty TargetMaterials;

        SerializedProperty CombineGroup;
        SerializedProperty CombineName;
        SerializedProperty DestroyOriginal;

        SerializedProperty ExportModel;
        SerializedProperty ExportPath;
        SerializedProperty ExportName;

        

        private bool _showList;

        public MenuTab_MeshTools()
        {
            GetProperties();
        }




        #region INTERFACE FUNCTIONS        
        public override void GetProperties()
        {
            toolsScript = ScriptableObject.CreateInstance<Tools_Mesh>();
            so = new SerializedObject(toolsScript);

            WillModifyLightmapUVs = so.FindProperty("_willModifyLightmapUVs");

            LightmapUVSet   = so.FindProperty("_uvSet");
            SourceGroup     = so.FindProperty("_sourceGroup");
            SourceObjects   = so.FindProperty("_sourceObjects");
            TargetMaterials = so.FindProperty("_targetMaterials");
            Prefix          = so.FindProperty("_prefix");

            CombineGroup    = so.FindProperty("_combineGroup");
            CombineName     = so.FindProperty("_combineParentName");
            DestroyOriginal = so.FindProperty("_destroyOldMesh");

            ExportModel     = so.FindProperty("_exportObject");
            ExportPath      = so.FindProperty("_exportPath");
            ExportName      = so.FindProperty("_exportName");
        }
        public override void DrawMenu(GUIStyle style)
        {
            DrawMenu_DuplicateGroup(style);
            DrawMenu_CombineGroup(style);


            // VALIDATION CHECKS
            #if USE_MESH_BAKER
                EditorGUILayout.LabelField("Mesh Baker Installed", CustomEditorUtilities.GoodTextStyle());
            #else
                EditorGUILayout.LabelField("Mesh Baker Not Installed", CustomEditorUtilities.BadTextStyle());
            #endif


            #if UNITY_FBX_EXPORT
                EditorGUILayout.LabelField("Unity FBX Export Installed", CustomEditorUtilities.GoodTextStyle());
                DrawMenu_Export(style);
            #else
                EditorGUILayout.LabelField("Unity FBX Export Not Installed", CustomEditorUtilities.BadTextStyle());
            #endif
        }
        #endregion


        #region MENU CALLS
        private void DrawMenu_DuplicateGroup(GUIStyle style)
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Duplicate Group Of Objects", style);

            EditorGUILayout.Space();

            EditorGUILayout.PropertyField(SourceGroup, true);

            EditorGUILayout.Space();

            // Use Atlased Texture - scale and offset uv0 set for the number of duplicates
            // tie this into the Texture Baker?
            // assign a bake group to these duplicates to determine number of duplicates and final material?

            EditorGUILayout.PropertyField(WillModifyLightmapUVs, new GUIContent("Modify Lightmap UVs?"));

            EditorGUILayout.EndVertical();

            if (WillModifyLightmapUVs.boolValue)
            {
                EditorGUILayout.BeginVertical("Box");

                EditorGUILayout.Space();

                EditorGUILayout.PropertyField(LightmapUVSet, new GUIContent("Lightmap UV Set: "));
                EditorGUILayout.PropertyField(Prefix, new GUIContent("Material Replacement Prefix: "));
                EditorGUILayout.PropertyField(TargetMaterials);

                if (SourceGroup.objectReferenceValue != null)
                {
                    EditorGUILayout.BeginVertical("Box");

                    Renderer[] renderers = toolsScript.GetSourceGroupRenderers();

                    if (renderers != null)
                    {
                        _showList = EditorGUILayout.Foldout(_showList, new GUIContent("Renderer List [" + renderers.Length + "]"));

                        if (_showList)
                        {
                            for (int i = 0; i < renderers.Length; i++)
                            {
                                if (renderers[i] != null)
                                {
                                    Renderer renderer = renderers[i].GetComponent<Renderer>();

                                    Vector2 scale = Tools_Lightmapping.GetLightmapScale(renderer);
                                    Vector2 offset = Tools_Lightmapping.GetLightmapPosition(renderer);

                                    EditorGUILayout.LabelField(renderers[i].name, EditorStyles.boldLabel);
                                    EditorGUILayout.LabelField("     Scale  [X: " + scale.x + ", Y: " + scale.y + "]");
                                    EditorGUILayout.LabelField("     Offset [X: " + offset.x + ", Y: " + offset.y + "]");
                                }
                            }
                        }
                    }

                    EditorGUILayout.EndVertical();
                }

                EditorGUILayout.EndVertical();
            }

            EditorGUILayout.BeginVertical("Box");

            if (GUILayout.Button("Duplicate Group"))
            {
                toolsScript.DuplicateGroup((GameObject)SourceGroup.objectReferenceValue, " [Lightmapped]");
            }

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();
        }
        Mesh combinedMesh;
        private void DrawMenu_CombineGroup(GUIStyle style)
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Combine Meshes", style);

            EditorGUILayout.Space();

            EditorGUILayout.PropertyField(CombineName);
            EditorGUILayout.PropertyField(CombineGroup);
            EditorGUILayout.PropertyField(DestroyOriginal);

            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Combine"))
            {
                toolsScript.CombineMeshes((GameObject)CombineGroup.objectReferenceValue);
            }

            EditorGUILayout.EndHorizontal();
            
            EditorGUILayout.Space();

            EditorGUILayout.PropertyField(ExportPath);

            combinedMesh = toolsScript.GetCombinedMesh();

            if(combinedMesh != null)
            {
                EditorGUILayout.LabelField("Target Directory:", EditorStyles.boldLabel);

                string path = Application.dataPath + "/" + ExportPath.stringValue + "/" + combinedMesh.name + ".mesh";

                GUIStyle existStyle = CustomEditorUtilities.MedTextStyle();

                if (Directory.Exists(path))
                {
                    existStyle = CustomEditorUtilities.BadTextStyle();
                }

                EditorGUILayout.LabelField(path, existStyle);

                if (GUILayout.Button("Save Mesh Asset"))
                {
                    SaveMeshAsset(combinedMesh, path);
                }
            }

            if (GUILayout.Button("Save Prefab"))
            {
                SavePrefab((GameObject)ExportModel.objectReferenceValue, ExportPath.stringValue, ExportName.stringValue);
            }

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();
        }

        private void DrawMenu_Export(GUIStyle style)
        {
            EditorGUILayout.BeginVertical("Box");

                EditorGUILayout.LabelField("Export Mesh as FBX", style);

                EditorGUILayout.Space();

                EditorGUILayout.PropertyField(ExportModel);

                EditorGUILayout.Space();
            
                EditorGUILayout.PropertyField(ExportName);

                EditorGUILayout.Space();

                EditorGUILayout.LabelField("Target Directory:", EditorStyles.boldLabel);

                string path = Application.dataPath + "/" + ExportPath.stringValue + ExportName.stringValue + ".fbx";
            
                GUIStyle existStyle = CustomEditorUtilities.MedTextStyle();

                if (Directory.Exists(path))
                {
                    existStyle = CustomEditorUtilities.BadTextStyle();
                }
                
                EditorGUILayout.LabelField(path, existStyle);

            EditorGUILayout.EndVertical();

            if (GUILayout.Button("Export"))
            {
                Export((GameObject)ExportModel.objectReferenceValue, ExportPath.stringValue, ExportName.stringValue);
            }

            EditorGUILayout.Space();
        }

        private void DrawMenu_Objects()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.PropertyField(SourceObjects, true);
            EditorGUILayout.PropertyField(LightmapUVSet);
            EditorGUILayout.PropertyField(TargetMaterials);

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();
        }

        private void DrawButton_Duplicate()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Duplicate"))
            {
                toolsScript.DuplicateObject();
            }

            if (GUILayout.Button("Clear"))
            {
                toolsScript.Clear();
            }

            EditorGUILayout.EndHorizontal();

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();
        }

        private void DrawCombineResults()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.PropertyField(ExportModel);

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();
        }
        #endregion


        #region ATLAS MODELS
        private GameObject _sourceObject;

        private string _targetName = "GeneratedObject";
        private string _targetModelFolder = "_models";
        private int _atlasDivisions = 4;

        private bool _makePrefabs = false;

        private string _targetPrefabFolder = "_prefabs";

        #pragma warning disable 0649
        [SerializeField] private MonoScript[] _componentsToAdd;
        [SerializeField] private Material[] _materialsToAdd;
        #pragma warning restore 0649

        private string _modelPath = "";
        private string _prefabPath = "";

        SerializedProperty MaterialsToAdd;
        SerializedProperty ComponentsToAdd;
        private void CreateModels()
        {
            Vector2 scale = new Vector2(1.0f / _atlasDivisions, 1.0f / _atlasDivisions);

            int numberOfModels = _atlasDivisions * _atlasDivisions;

            // go through each atlased version
            for (int i = 0; i < numberOfModels; i++)
            {
                // create grid values of atlas
                int column  = Mathf.RoundToInt(i % _atlasDivisions);
                int row     = Mathf.RoundToInt(Mathf.Floor(i / _atlasDivisions));

                // update offset value along grid placement
                Vector2 offset = new Vector2(column, row) * scale;

                // instantiate the source object
                GameObject instantiatedObject = UnityEngine.Object.Instantiate(_sourceObject);

                // rename new instantiated object
                instantiatedObject.name = _targetName + "_" + i;

                // get all the meshes (child meshes should be LOD versions)
                MeshFilter[] meshFilters = instantiatedObject.GetComponentsInChildren<MeshFilter>();

                // start our count
                int lodCount = 0;

                // go through each LOD level model...
                foreach (var meshFilter in meshFilters)
                {
                    List<Vector2> originalUVs = new List<Vector2>();
                    List<Vector2> newUVs      = new List<Vector2>();

                    // create a new mesh object for each LOD level
                    Mesh newMesh        = new Mesh();
                    newMesh.name        = "User_" + i + "_LOD" + lodCount;

                    // copy over the original mesh values
                    newMesh.bindposes   = meshFilter.sharedMesh.bindposes;
                    newMesh.boneWeights = meshFilter.sharedMesh.boneWeights;
                    newMesh.bounds      = meshFilter.sharedMesh.bounds;
                    newMesh.colors      = meshFilter.sharedMesh.colors;
                    newMesh.normals     = meshFilter.sharedMesh.normals;
                    newMesh.tangents    = meshFilter.sharedMesh.tangents;
                    newMesh.triangles   = meshFilter.sharedMesh.triangles;
                    newMesh.vertices    = meshFilter.sharedMesh.vertices;

                    // Store the original UV set 0 as a list
                    meshFilter.sharedMesh.GetUVs(0, originalUVs);

                    // update count for next iteration
                    lodCount++;

                    // loop through all the uvs 
                    for (int j = 0; j < originalUVs.Count; j++)
                    {
                        // and scale & offset them
                        Vector4 scaledUV = originalUVs[j] * scale + offset;

                        // add updated UVs to the newUV list
                        newUVs.Add(scaledUV);
                    }

                    // Pass the new UV list to our new mesh's uv set 0
                    newMesh.SetUVs(0, newUVs);

                    // replace our mesh filter's mesh with the new one
                    meshFilter.mesh = newMesh;

                    // clear out our lists
                    originalUVs.Clear();
                    newUVs.Clear();
                }

                ExportModelObject(instantiatedObject, i);

                if (_makePrefabs)
                {
                    CreatePrefab();
                }

                // Clear out our scene hierarchy
                UnityEngine.Object.DestroyImmediate(instantiatedObject);
            }
        }

        private void CreatePrefab()
        {
            // load new model into memory
            GameObject loadedModel = (GameObject)AssetDatabase.LoadMainAssetAtPath(_modelPath);

            // instantiate loaded model into scene
            GameObject instantiatedModel = (GameObject)PrefabUtility.InstantiatePrefab(loadedModel);

            // apply components to new model
            AddComponents(instantiatedModel);

            // apply material to new model
            AddMaterials(instantiatedModel);

            // Save out model as a Prefab
            //SavePrefab(instantiatedModel, _prefabPath);

            // Clear out our scene hierarchy
            UnityEngine.Object.DestroyImmediate(instantiatedModel, true);
        }

        private void AddComponents(GameObject go)
        {
            if (_componentsToAdd != null && _componentsToAdd.Length > 0)
            {
                for (int i = 0; i < _componentsToAdd.Length; i++)
                {
                    go.AddComponent(_componentsToAdd[i].GetClass());
                }
            }

        }

        private void AddMaterials(GameObject go)
        {
            Renderer[] renderers = go.GetComponentsInChildren<Renderer>();
            foreach (Renderer renderer in renderers)
            {
                if (_materialsToAdd != null && _materialsToAdd.Length > 0 && _materialsToAdd.Length == renderer.sharedMaterials.Length)
                {
                    for (int i = 0; i < renderer.sharedMaterials.Length; i++)
                    {
                        if (_materialsToAdd[i] != null)
                        {
                            // only using 1 material right now
                            renderer.sharedMaterial = _materialsToAdd[i];
                        }
                    }
                }
            }
        }

        #endregion


        #region SAVE & EXPORT
        private string GetPath(GameObject sourceObject)
        {
            string fileName = "/" + sourceObject.name + ".fbx";
            string path = AssetDatabase.GetAssetPath(sourceObject);

            // remove file name from path string
            path = path.Replace(fileName, "");

            return path;
        }
        private void SavePrefab(GameObject go, string path, string name)
        {
            // create prefab folder location - SaveAsPrefab won't work unless the folder already exists
            if (!Directory.Exists(path))
            {
                AssetDatabase.CreateFolder(CustomEditorUtilities.GetPath(go), path);
            }

            MeshFilter[] meshFitlers = go.GetComponentsInChildren<MeshFilter>();

            foreach (MeshFilter meshFilter in meshFitlers)
            {
                SaveMeshAsset(meshFilter.sharedMesh, path);
            }

            // save as prefab
            var variantRoot = PrefabUtility.SaveAsPrefabAsset(go, path + name + ".prefab");

            // destroy scene instance
            //DestroyImmediate(go, true);
            go.SetActive(false);
        }

        private void SaveMeshAsset(Mesh mesh, string path)
        {
            if (!Directory.Exists(path))
            {
                AssetDatabase.CreateFolder("Assets", ExportPath.stringValue + "/" + combinedMesh.name + ".mesh");
            }

            //byte[] bytes = MeshSerializer.WriteMesh(mesh, true);
            //File.WriteAllBytes(path, bytes);

            AssetDatabase.CreateAsset(mesh, path);
            AssetDatabase.SaveAssets();
        }

        private void Export(GameObject target, string exportPath, string exportName)
        {
            #if UNITY_FBX_EXPORT
            if (target == null)
            {
                Debug.LogWarning("Export Model Has Not Been Set");
                return;
            }

            string path = Application.dataPath + "/" + exportPath + exportName + ".fbx";

            ModelExporter.ExportObject(path, target);
            #endif
        }

        private void ExportModelObject(GameObject target, int userIndex)
        {
            #if UNITY_FBX_EXPORT
            _modelPath = GetPath(target) + "/" + _targetModelFolder + "/" + _targetName + "_" + userIndex + ".fbx";
            _prefabPath = GetPath(target) + "/" + _targetPrefabFolder + "/" + _targetName + "_" + userIndex + ".prefab";

            // REQUIRES Unity's FBX Exporter Package
            ModelExporter.ExportObject(_modelPath, target);
            #endif
        }
        #endregion


    }
}