using System.IO;
using System.Collections.Generic;
using System.Text;

using UnityEditor;
using UnityEditor.SceneManagement;

using UnityEngine;

using VRR.TechArtTools.Menu;


namespace VRR.TechArtTools.GeneralTools
{
    public class MenuTab_GeneralTools : MenuTab<Tools_General>
    {
        SerializedProperty SubFolderNames;
        SerializedProperty MatchOrientation;

        private static string Symbol = "YOUR_DEFINE_SYMBOL";

        public MenuTab_GeneralTools()
        {
            GetProperties();
        }


        #region INTERFACE FUNCTIONS
       
        public override void GetProperties()
        {
            toolsScript = ScriptableObject.CreateInstance<Tools_General>();
            so = new SerializedObject(toolsScript);

            SubFolderNames = so.FindProperty("_subFolders");
            MatchOrientation = so.FindProperty("_matchOrientation");
        }

        public override void DrawMenu(GUIStyle style)
        {
            DrawMenu_SymbolDefines(style);
            DrawMenu_QuickSceneSetup(style);
            DrawMenu_QuickFolderCreation(style);
            DrawMenu_FindReplaceMaterial(style);
            DrawMenu_MatchPose(style);
        }
        #endregion

        #region MENU CALLS

        #region STATISTICS
        public void DrawMenu_DisplayVertCount(GUIStyle style)
        {
            EditorGUILayout.Space();

            EditorGUILayout.BeginHorizontal();
                DrawCount_Selected(style);
                DrawCount_SceneTotal(style);
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.Space();
        }

        Dictionary<GameObject, int> SceneVertCount;
        Dictionary<Material, int> SceneMaterials;

        Dictionary<GameObject, int> SelectedVertCount;
        Dictionary<Material, int> SelectedMaterials;
        private void DrawCount_Selected(GUIStyle style)
        {
            EditorGUILayout.BeginVertical("box");

                EditorGUILayout.LabelField("Selected", style);
                EditorGUILayout.Space();

                Vector3Int total = toolsScript.GetTotalVertsAndTriCount(GetSelectedMeshFilters(), GetSelectedSkinMeshRenderers());

                string text = " meshes";

                if (total.z == 1) { text = " mesh"; }

                EditorGUILayout.LabelField("Selection: " + total.z + text);
                EditorGUILayout.LabelField("Vertex Count: " + total.x);
                EditorGUILayout.LabelField("Tri Count: " + total.y);

                DrawMenu_SelectedMaterialCount();

            EditorGUILayout.EndVertical();
        }
        private void DrawCount_SceneTotal(GUIStyle style)
        {
            EditorGUILayout.BeginVertical("box");

                EditorGUILayout.LabelField("Scene Total", style);
                EditorGUILayout.Space();

                Vector3Int total = toolsScript.GetTotalVertsAndTriCount(GameObject.FindObjectsOfType<MeshFilter>(), GameObject.FindObjectsOfType<SkinnedMeshRenderer>());

                string text = " meshes";

                if (total.z == 1) { text = " mesh"; }


                EditorGUILayout.LabelField("Meshes in Scene: " + total.z + text);
                EditorGUILayout.LabelField("Vertex Count: " + total.x);
                EditorGUILayout.LabelField("Tri Count: " + total.y);

                DrawMenu_SceneMaterialCount();

            EditorGUILayout.EndVertical();
        }

        private Vector2 _scrollPos_SceneMats;
        private void DrawMenu_SceneMaterialCount()
        {
            SceneMaterials = toolsScript.GetListOfMaterialPassCount(GameObject.FindObjectsOfType<MeshFilter>(), GameObject.FindObjectsOfType<SkinnedMeshRenderer>());

            EditorGUILayout.LabelField("Number Of Materials: " + SceneMaterials.Count, EditorStyles.boldLabel);

            if (SceneMaterials.Count > 0)
            {
                EditorGUILayout.BeginVertical("Box");

                    _scrollPos_SceneMats = EditorGUILayout.BeginScrollView(_scrollPos_SceneMats, false, false, GUILayout.Height(100));

                        int i = 0;
                        foreach (var kvp in SceneMaterials)
                        {
                            EditorGUILayout.BeginHorizontal();
                                EditorGUILayout.LabelField(i + ") " + kvp.Key.name, GUILayout.Width(120));
                                EditorGUILayout.LabelField("[" + kvp.Value + " passes]", GUILayout.Width(60));
                            EditorGUILayout.EndHorizontal();

                            i++;
                        }

                    EditorGUILayout.EndScrollView();

                    EditorGUILayout.LabelField("Material Passes: " + toolsScript.TotalMaterialPassCount(SceneMaterials), EditorStyles.boldLabel);

                EditorGUILayout.EndVertical();
            }
        }

        private Vector2 _scrollPos_SelectedMats;
        private void DrawMenu_SelectedMaterialCount()
        {
            SelectedMaterials = toolsScript.GetListOfMaterialPassCount(GetSelectedMeshFilters(), GetSelectedSkinMeshRenderers());

            if (SelectedMaterials == null || SelectedMaterials.Count == 0)
            {
                EditorGUILayout.LabelField("Number Of Materials: 0", EditorStyles.boldLabel);
                return;
            }

            EditorGUILayout.LabelField("Number Of Materials: " + SelectedMaterials.Count, EditorStyles.boldLabel);

            if (SelectedMaterials.Count > 0)
            {
                EditorGUILayout.BeginVertical("Box");

                    _scrollPos_SelectedMats = EditorGUILayout.BeginScrollView(_scrollPos_SelectedMats, false, false, GUILayout.Height(100));

                        int i = 0;
                        foreach (var kvp in SelectedMaterials)
                        {
                            EditorGUILayout.BeginHorizontal();
                                EditorGUILayout.LabelField(i + ") " + kvp.Key.name, GUILayout.Width(120));
                                EditorGUILayout.LabelField("[" + kvp.Value + " passes]", GUILayout.Width(60));
                            EditorGUILayout.EndHorizontal();

                            i++;
                        }

                    EditorGUILayout.EndScrollView();

                    EditorGUILayout.LabelField("Material Passes: " + toolsScript.TotalMaterialPassCount(SelectedMaterials), EditorStyles.boldLabel);

                EditorGUILayout.EndVertical();
            }
        }


        private MeshFilter[] GetSelectedMeshFilters()
        {
            if (Selection.gameObjects == null)
            {
                return null;
            }

            List<MeshFilter> allMeshFilters = new List<MeshFilter>();

            foreach (var go in Selection.gameObjects)
            {
                MeshFilter[] mf = go.GetComponentsInChildren<MeshFilter>();
                if (mf != null && mf.Length > 0)
                {
                    for (int i = 0; i < mf.Length; i++)
                    {
                        allMeshFilters.Add(mf[i]);
                    }
                }
            }

            if (allMeshFilters.Count > 0)
            {
                return allMeshFilters.ToArray();
            }
            else
            {
                return null;
            }
        }
        private SkinnedMeshRenderer[] GetSelectedSkinMeshRenderers()
        {
            if (Selection.gameObjects == null)
            {
                return null;
            }

            List<SkinnedMeshRenderer> allSkinnedMeshes = new List<SkinnedMeshRenderer>();

            foreach (var go in Selection.gameObjects)
            {
                SkinnedMeshRenderer[] skin = go.GetComponentsInChildren<SkinnedMeshRenderer>();
                if (skin != null && skin.Length > 0)
                {
                    for (int i = 0; i < skin.Length; i++)
                    {
                        allSkinnedMeshes.Add(skin[i]);
                    }
                }
            }

            if (allSkinnedMeshes.Count > 0)
            {
                return allSkinnedMeshes.ToArray();
            }
            else
            {
                return null;
            }

        }
        public Vector3Int CountVertsAndTrisInCurrentSelection()
        {
            int vertexCount = 0;
            int triangleCount = 0;

            GameObject[] selectedObjects = Selection.gameObjects;

            if (selectedObjects == null || selectedObjects.Length == 0)
            {
                if (SelectedMaterials != null)
                {
                    SelectedMaterials.Clear();
                    SelectedMaterials = null;
                }

                return Vector3Int.zero;
            }


            List<MeshFilter> allMeshFilters = new List<MeshFilter>();
            List<SkinnedMeshRenderer> allSkinnedMeshes = new List<SkinnedMeshRenderer>();
            SelectedMaterials = new Dictionary<Material, int>();

            foreach (var go in selectedObjects)
            {
                MeshFilter[] mf = go.GetComponentsInChildren<MeshFilter>();
                if (mf != null && mf.Length > 0)
                {
                    for (int i = 0; i < mf.Length; i++)
                    {
                        allMeshFilters.Add(mf[i]);
                    }
                }

                SkinnedMeshRenderer[] skin = go.GetComponentsInChildren<SkinnedMeshRenderer>();
                if (skin != null && skin.Length > 0)
                {
                    for (int i = 0; i < skin.Length; i++)
                    {
                        allSkinnedMeshes.Add(skin[i]);
                    }
                }
            }


            for (int i = 0; i < allMeshFilters.Count; i++)
            {
                Mesh sharedMesh = allMeshFilters[i].sharedMesh;

                if (sharedMesh != null && allMeshFilters[i].gameObject.activeInHierarchy)
                {
                    vertexCount += toolsScript.GetVertCount(sharedMesh);
                    triangleCount += toolsScript.GetTriCount(sharedMesh);
                }

                foreach (var mat in toolsScript.GetMaterials(allMeshFilters[i].gameObject.GetComponent<Renderer>()))
                {
                    if (SelectedMaterials != null && !SelectedMaterials.ContainsKey(mat))
                    {
                        SelectedMaterials.Add(mat, mat.passCount);
                    }
                }
            }

            for (int i = 0; i < allSkinnedMeshes.Count; i++)
            {
                Mesh sharedMesh = allSkinnedMeshes[i].sharedMesh;

                if (sharedMesh != null && allSkinnedMeshes[i].gameObject.activeInHierarchy)
                {
                    vertexCount += toolsScript.GetVertCount(sharedMesh);
                    triangleCount += toolsScript.GetTriCount(sharedMesh);
                }

                foreach (var mat in toolsScript.GetMaterials(allSkinnedMeshes[i].gameObject.GetComponent<Renderer>()))
                {
                    if (SelectedMaterials != null && !SelectedMaterials.ContainsKey(mat))
                    {
                        SelectedMaterials.Add(mat, mat.passCount);
                    }
                }
            }

            Vector3Int finalCount = new Vector3Int(vertexCount, triangleCount, allMeshFilters.Count + allSkinnedMeshes.Count);

            return finalCount;
        }
        #endregion


        #region PROJECT HELPERS
        private void DrawMenu_QuickSceneSetup(GUIStyle style)
        {
            EditorGUILayout.LabelField("Quick Scene Setup", style);

            EditorGUILayout.Space();
            
            EditorGUILayout.BeginHorizontal("Box");
                EditorGUILayout.Space();
                if (GUILayout.Button("New Scene", GUILayout.Width(200)))
                {
                    CreateNewScene();
                }
                EditorGUILayout.Space();
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.Space();
        }
        private void DrawMenu_QuickFolderCreation(GUIStyle style)
        {
            EditorGUILayout.LabelField("Quick Folder Setup", style);

            EditorGUILayout.BeginVertical("box");
                DrawMenu_DisplayCurrentlySelectedFolder();
                EditorGUILayout.Space();
                DrawMenu_NewFolderStructure();
            EditorGUILayout.EndVertical();
        }

        #region MENUS
        private void DrawMenu_DisplayCurrentlySelectedFolder()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Selected Folder: ");

            EditorGUILayout.LabelField(GetCurrentAssetDirectory());

            EditorGUILayout.EndVertical();
        }

        private void DrawMenu_NewFolderStructure()
        {
            EditorGUILayout.BeginHorizontal();

            EditorGUILayout.Space();

            EditorGUILayout.BeginVertical("box");
            
            SubFolderNames.arraySize = EditorGUILayout.IntField("Number of Sub-Folders", SubFolderNames.arraySize, GUILayout.Width(200));

            if (SubFolderNames.arraySize <= 0)
            {
                toolsScript.SetDefaultValues();
            }
            else
            {
                for (int i = 0; i < SubFolderNames.arraySize; i++)
                {
                    SubFolderNames.GetArrayElementAtIndex(i).stringValue = EditorGUILayout.TextField(SubFolderNames.GetArrayElementAtIndex(i).stringValue, GUILayout.Width(200));
                }

                if (GUILayout.Button("Create Sub-Folders", GUILayout.Width(200)))
                {
                    CreateFolders();
                }
            }

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();

            EditorGUILayout.EndHorizontal();
        }
        #endregion

        #region FUNCTIONS
        private void CreateNewScene()
        {
            EditorSceneManager.NewScene(NewSceneSetup.EmptyScene);

            GameObject root     = new GameObject("SYSTEM");
            GameObject ui       = new GameObject("UI");


            GameObject lights   = new GameObject("LIGHTING");
            GameObject baked    = new GameObject("BAKED");
            baked.transform.SetParent(lights.transform);
            GameObject real     = new GameObject("REALTIME");
            real.transform.SetParent(lights.transform);
            GameObject probes   = new GameObject("PROBES");
            probes.transform.SetParent(lights.transform);


            GameObject env      = new GameObject("ENVIRONMENT");


            GameObject npcs     = new GameObject("NPCs");


            //GameObject[] childObjects = new GameObject[2];

            //foreach (GameObject obj in childObjects)
            //{
            //    obj.transform.SetParent(root.transform);
            //}
        }

        private void CreateFolders()
        {
            var path = string.Empty;

            var folders = Selection.objects;

            path = GetCurrentAssetDirectory();

            if (path.Length > 0 && path != "Assets")
            {
                if (Directory.Exists(path))
                {
                    Debug.Log("Folder");

                    for (int i = 0; i < SubFolderNames.arraySize; i++)
                    {
                        if (SubFolderNames.GetArrayElementAtIndex(i).stringValue != string.Empty)
                        {
                            string newPath = path + "/" + SubFolderNames.GetArrayElementAtIndex(i).stringValue;
                            Debug.Log("Creating: " + newPath);
                            Directory.CreateDirectory(newPath);
                        }
                    }
                }
                else
                {
                    Debug.Log("Select a Folder, not a File");
                }
            }
            else
            {
                Debug.Log("Not in assets folder");
            }

            AssetDatabase.Refresh();

        }

        public string GetCurrentAssetDirectory()
        {
            foreach (var obj in Selection.GetFiltered<Object>(SelectionMode.Assets))
            {
                var path = AssetDatabase.GetAssetPath(obj);
                if (string.IsNullOrEmpty(path))
                    continue;

                if (System.IO.Directory.Exists(path))
                    return path;
                else if (System.IO.File.Exists(path))
                    return System.IO.Path.GetDirectoryName(path);
            }

            return "Assets";
        }
        #endregion
        #endregion


        #region MATERIAL REPLACEMENT
        public Material MaterialToReplace;
        public Material NewMaterial;
        public bool AffectAllInScene;
        private void DrawMenu_FindReplaceMaterial(GUIStyle style)
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.LabelField("Quick Material Replacement", style);

            EditorGUILayout.Space();

            EditorGUILayout.BeginVertical("Box");
            MaterialToReplace = (Material)EditorGUILayout.ObjectField("Find This: ", MaterialToReplace, typeof(Material), false);
            NewMaterial = (Material)EditorGUILayout.ObjectField("Replace With: ", NewMaterial, typeof(Material), false);
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();

            AffectAllInScene = EditorGUILayout.Toggle("Affect ALL Materials? ", AffectAllInScene);

            EditorGUILayout.Space();

            if (MaterialToReplace != null && NewMaterial != null)
            {
                if (GUILayout.Button("Replace Materials"))
                {
                    ReplaceMaterial();
                }
            }

            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();
        }

        #region FUNCTIONS
        public void ReplaceMaterial()
        {
            if (MaterialToReplace != null && NewMaterial != null)
            {
                Renderer[] sceneRenderers;

                sceneRenderers = GameObject.FindObjectsOfType<Renderer>();

                for (int i = 0; i < sceneRenderers.Length; i++)
                {
                    if (sceneRenderers[i].sharedMaterial == MaterialToReplace)
                    {
                        sceneRenderers[i].sharedMaterial = NewMaterial;
                    }
                }
            }
        }
        public void AssignMaterialToAll()
        {
            if (NewMaterial != null)
            {
                Renderer[] sceneRenderers;

                sceneRenderers = GameObject.FindObjectsOfType<Renderer>();

                for (int i = 0; i < sceneRenderers.Length; i++)
                {
                    if (sceneRenderers[i].gameObject.activeInHierarchy)
                    {
                        sceneRenderers[i].materials = new Material[1];
                        sceneRenderers[i].sharedMaterial = NewMaterial;
                    }
                }
            }
        }
        #endregion
        #endregion


        #region MATCH POSE
        Transform SourceTransform, TargetTransform;
        
        private void DrawMenu_MatchPose(GUIStyle style)
        {
            EditorGUILayout.LabelField("Quick Pose Match", style);

            EditorGUILayout.Space();

            SourceTransform = (Transform)EditorGUILayout.ObjectField("Match this Object:", SourceTransform, typeof(Transform), true);
            TargetTransform = (Transform)EditorGUILayout.ObjectField("To this Object:", TargetTransform, typeof(Transform), true);

            EditorGUILayout.PropertyField(MatchOrientation);

            if (SourceTransform != null && TargetTransform != null)
            {
                if (GUILayout.Button("Match"))
                {
                    toolsScript.MatchPose(SourceTransform, TargetTransform);
                }
            }
        }
        #endregion

        #endregion

        #region CUSTOM DEFINES
        // Inspiration from Lane Fox
        // https://gist.github.com/LaneF

        private void DrawMenu_SymbolDefines(GUIStyle style)
        {
            BuildTargetGroup currentGroup = EditorUserBuildSettings.selectedBuildTargetGroup;
            string currentSymbols = PlayerSettings.GetScriptingDefineSymbolsForGroup(currentGroup);
            string[] symbols = SplitSymbols(currentSymbols);

            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Symbol Defines", style);
            EditorGUILayout.Space();

            EditorGUILayout.LabelField("Current Defines:");
            EditorGUILayout.BeginVertical("box");
           
            for(int i = 0; i < symbols.Length; i++)
            {
                if (symbols[0] == string.Empty)
                {
                    EditorGUILayout.LabelField("<none>");
                }
                else
                {
                    EditorGUILayout.LabelField(i + ") " + symbols[i]);
                }
            }

            EditorGUILayout.EndVertical();

            Symbol = EditorGUILayout.TextField("Symbol to add: ", Symbol);

            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Add Symbol"))
            {
                AddDefine(Symbol);
            }

            if (GUILayout.Button("Remove Symbol"))
            {
                RemoveDefine(Symbol);
            }

            EditorGUILayout.EndHorizontal();

            EditorGUILayout.Space();
        }

        private static string[] SplitSymbols(string input)
        {
            return input.Split(new string[] { ";" }, System.StringSplitOptions.None);
        }

        private static void AddDefine(string def)
        {
            BuildTargetGroup currentGroup = EditorUserBuildSettings.selectedBuildTargetGroup;
            
            string currentSymbols = PlayerSettings.GetScriptingDefineSymbolsForGroup(currentGroup);

            StringBuilder newSymbolString = new StringBuilder();

            if (ValidateDuplicateDefines(def)) { Debug.Log($"<color=orange>{def} already defined!</color>"); return; }
            
            newSymbolString.Append(currentSymbols);

            if (newSymbolString.Length == 0)
            {
                newSymbolString.Append(def + ";");
            }
            else
            { 
                newSymbolString.Append($"{(newSymbolString[newSymbolString.Length - 1] == ';' ? "" : ";")}{def}"); 
            }
            
            PlayerSettings.SetScriptingDefineSymbolsForGroup(currentGroup, newSymbolString.ToString());
        }
        private static void RemoveDefine(string def)
        {
            BuildTargetGroup currentGroup = EditorUserBuildSettings.selectedBuildTargetGroup;

            string currentSymbols = PlayerSettings.GetScriptingDefineSymbolsForGroup(currentGroup);

            PlayerSettings.SetScriptingDefineSymbolsForGroup(currentGroup, currentSymbols.Replace(def, ""));
        }
        private static bool ValidateDuplicateDefines(string def)
        {
            BuildTargetGroup currentGroup = EditorUserBuildSettings.selectedBuildTargetGroup;
            string currentSymbols = PlayerSettings.GetScriptingDefineSymbolsForGroup(currentGroup);

            string[] symbols = SplitSymbols(currentSymbols);

            foreach(var sym in symbols)
            {
                if(sym == def)
                {
                    return true;
                }
            }

            return false;
        }
        #endregion
    }
}