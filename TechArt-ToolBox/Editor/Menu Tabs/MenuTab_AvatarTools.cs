using UnityEditor;
using UnityEngine;

using VRR.TechArtTools.Menu;
using VRR.TechArtTools.Utilities;

#if USE_MESH_BAKER
using VRR.Integrations.MeshBaker;
#endif

#if USE_FINAL_IK
using VRR.Integrations.FinalIK;
#endif


namespace VRR.TechArtTools.Avatar
{
    public class MenuTab_AvatarTools : MenuTab<Tools_Avatar>
    {
        // SET UP ACTOR
        private GameObject _mesh;
        private GameObject _skeleton;

        // COMBINE OPTION
        private bool _wantToCombine;

        // ACTOR Meshes to combine
        SerializedProperty MeshObjects;

        // GO reference to the Baking setup for easy deletion
        private GameObject _bakingSetupParent;

        // SETUP NPC
        private GameObject _selectedActor;
        private string _newName = "PREFAB NAME";



        public MenuTab_AvatarTools()
        {
            GetProperties();
        }

        #region INTERFACE FUNCTIONS
        public override void GetProperties()
        {
            if (toolsScript == null || so == null)
            {
                toolsScript = ScriptableObject.CreateInstance<Tools_Avatar>();
                so = new SerializedObject(toolsScript);

                MeshObjects = so.FindProperty("_meshObjects");
            }
        }

        public override void DrawMenu(GUIStyle style)
        {
            DrawMenu_ActorSetup(style);

            EditorGUILayout.Space();

            DrawMenu_NPCSetup(style);
        }
        #endregion


        #region MENU CALLS  
        private void DrawMenu_ActorSetup(GUIStyle style)
        {
            EditorGUILayout.LabelField("Setup ACTOR", style);

            EditorGUILayout.Space();
            _wantToCombine = EditorGUILayout.Toggle("Combine NPC Parts?", _wantToCombine);

            if (!_wantToCombine)
            {
                DrawMenu_OnlyActorSetup();
            }
            else
            {
                EditorGUILayout.LabelField("Combine Meshes (and Setup Actor):", EditorStyles.boldLabel);
                DrawMenu_ModelInputs();
                DrawMenu_MeshBakerCombiner();
            }

            if (_bakingSetupParent != null)
            {
                DrawButton_ClearBaker();
            }
        }

        private void DrawMenu_OnlyActorSetup()
        {
            EditorGUILayout.LabelField("Setup Actor Object:", EditorStyles.boldLabel);

            EditorGUILayout.BeginVertical("box");

            _mesh = (GameObject)EditorGUILayout.ObjectField("Mesh: ", _mesh, typeof(GameObject), true);
            _skeleton = (GameObject)EditorGUILayout.ObjectField("Skelton: ", _skeleton, typeof(GameObject), true);

            if (_mesh != null && _skeleton != null)
            {
                if (GUILayout.Button("Setup Actor"))
                {
                    toolsScript.SetupSkeletonComponents(_skeleton);
                    toolsScript.SetupMeshComponents(_mesh);
                }
            }

            EditorGUILayout.EndVertical();
        }

        private void DrawMenu_ModelInputs()
        {
            #if USE_MESH_BAKER
                EditorGUILayout.LabelField("Mesh Baker Installed", CustomEditorUtilities.GoodTextStyle());
                
                EditorGUILayout.BeginVertical("box");
                    EditorGUILayout.PropertyField(MeshObjects);
                EditorGUILayout.EndHorizontal();
            #else
                EditorGUILayout.LabelField("Mesh Baker Not Installed or Symbol NOT Defined [USE_MESH_BAKER]", CustomEditorUtilities.BadTextStyle());
            #endif
        }

        private void DrawMenu_MeshBakerCombiner()
        {
            #if USE_MESH_BAKER

            // if we don't have all the meshes to combine, don't show the button
            GameObject[] meshesToCombine = toolsScript.GetMeshes();

            if (meshesToCombine == null || meshesToCombine.Length == 0) { return; }

            // don't display button until ALL SLOTS ARE FILLED
            for (int i = 0; i < meshesToCombine.Length; i++)
            {
                if (meshesToCombine[i] == null)
                {
                    return;
                }
            }

            if (GUILayout.Button("Combine!"))
            {
                // CREATE OUR PARENT GAME OBJECT FOR EASY DELETION LATER
                _bakingSetupParent = new GameObject("BAKING SETUP");


                // CREATE BAKER
                GameObject bakerObject = MeshBakerIntegration.CreateBaker();
                bakerObject.transform.SetParent(_bakingSetupParent.transform);

                // BRING OBJECTS INTO SCENE
                GameObject[] meshes = toolsScript.InstantiateActorBakingObjects(bakerObject, toolsScript.GetMeshes());


                // BAKE OBJECTS!
                Transform bakedParent = MeshBakerIntegration.BakeMeshes(meshes, bakerObject);
                // rename new mesh
                GameObject bakedMesh = bakedParent.GetChild(0).gameObject;
                bakedMesh.name = "MESH";


                // CONSOLIDATE THE BAKED MESH ONTO ONE SKELETON
                toolsScript.skeleton.transform.SetParent(bakedParent.transform);
                toolsScript.ReSkinMesh(bakedMesh.GetComponent<SkinnedMeshRenderer>(), toolsScript.skeleton, false);


                // SAVE THE MESH AS AN ASSET
                string targetPath = AssetDatabase.GetAssetPath(toolsScript.GetMeshes()[0]);
                targetPath = targetPath.Replace(toolsScript.GetMeshes()[0].name + ".fbx", "");
                SaveCombinedMeshAsset(bakedMesh, targetPath);


                // ADD COMPONENTS
                toolsScript.SetupSkeletonComponents(toolsScript.skeleton);
                toolsScript.SetupMeshComponents(bakedMesh);


                // SAVE ACTOR PREFAB
                SaveActorPrefab(bakedParent.gameObject, targetPath);

                // CLEAR BAKER
                GameObject.DestroyImmediate(_bakingSetupParent);
            }
            #endif
        }
        private void DrawButton_ClearBaker()
        {
            if (GUILayout.Button("Clear Baker"))
            {
                GameObject.DestroyImmediate(_bakingSetupParent);
            }
        }

        string _leftEyeBone = "Eyeball_L";
        string _rightEyeBone = "Eyeball_R";
        AnimationClip _speakClip;
        private void DrawMenu_NPCSetup(GUIStyle style)
        {
            EditorGUILayout.LabelField("Setup Components", style);

            #if USE_FINAL_IK
                EditorGUILayout.Space();
                EditorGUILayout.LabelField("Final IK Installed", CustomEditorUtilities.GoodTextStyle());
            #else
                EditorGUILayout.Space();
                EditorGUILayout.LabelField("Final IK Not Installed or Symbol NOT Defined [USE_FINAL_IK]", CustomEditorUtilities.BadTextStyle());
            #endif

            EditorGUILayout.BeginVertical("box");
            _selectedActor = (GameObject)EditorGUILayout.ObjectField("ACTOR Prefab: ", _selectedActor, typeof(GameObject), false);
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();

            EditorGUILayout.BeginVertical("box");
            _newName = (string)EditorGUILayout.TextField("NPC Prefab Name:", _newName);
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();

            EditorGUILayout.BeginVertical("box");
            EditorGUILayout.LabelField("Object Of Interest Setup:", EditorStyles.boldLabel);
            _leftEyeBone = (string)EditorGUILayout.TextField("Left Eye: ", _leftEyeBone);
            _rightEyeBone = (string)EditorGUILayout.TextField("Right Eye: ", _rightEyeBone);
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();

            EditorGUILayout.BeginVertical("box");
            EditorGUILayout.LabelField("Speech Injector Setup:", EditorStyles.boldLabel);
            _speakClip = (AnimationClip)EditorGUILayout.ObjectField("Speak Clip To Replace:", _speakClip, typeof(AnimationClip), false);
            EditorGUILayout.EndVertical();

            if (_newName != string.Empty && _selectedActor != null)
            {
                if (GUILayout.Button("Create NPC Prefab"))
                {
                    // Create NPC Parent Object and add base components
                    GameObject PREFAB = new GameObject(_newName);
                    toolsScript.SetupPrefabComponents(PREFAB, _selectedActor);

                    // SAVE NPC PREFAB
                    string targetPath = AssetDatabase.GetAssetPath(_selectedActor);
                    targetPath = targetPath.Replace(_selectedActor.name + ".prefab", "");

                    SaveNPCPrefab(PREFAB, targetPath);
                }
            }
        }
        #endregion


        #region SAVING
        /// <summary>
        /// Save the combined mesh as an asset so it can be used in other scenes and as a Prefab
        /// </summary>
        /// <param name="bakedMesh"></param>
        /// <param name="path"></param>
        private void SaveCombinedMeshAsset(GameObject bakedMesh, string path)
        {
            var smr = bakedMesh.GetComponent<SkinnedMeshRenderer>();
            if (smr)
            {
                var savePath = path + bakedMesh.name + ".asset";
                Debug.Log("Saved Mesh to:" + savePath);

                AssetDatabase.CreateAsset(smr.sharedMesh, savePath);
            }
        }
        private void SaveActorPrefab(GameObject actorRoot, string path)
        {
            var savePath = path + actorRoot.name + ".prefab";
            Debug.Log("Saved ACTOR Prefab to:" + savePath);

            // pass saved actor onto the NPC Setup Section
            _selectedActor = PrefabUtility.SaveAsPrefabAssetAndConnect(actorRoot, savePath, InteractionMode.UserAction);
        }
        private void SaveNPCPrefab(GameObject npcRoot, string path)
        {
            var savePath = path + npcRoot.name + ".prefab";
            Debug.Log("Saved NPC Prefab to:" + savePath);

            PrefabUtility.SaveAsPrefabAssetAndConnect(npcRoot, savePath, InteractionMode.UserAction);
        }
        #endregion
    }
}