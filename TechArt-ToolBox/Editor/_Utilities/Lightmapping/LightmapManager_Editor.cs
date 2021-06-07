using UnityEditor;
using UnityEngine;

namespace VRR.TechArtTools.Lightmapping 
{
    [CustomEditor(typeof(LightmapManager))]
    public class LightmapManager_Editor : UnityEditor.Editor 
    {
        LightmapManager _myTarget;
        SerializedObject _so;

        SerializedProperty Renderers;
        SerializedProperty LightmapsTextures;


        #region MONOBEHAVIORS
        private void OnEnable() 
        {
            if (_myTarget == null || _so == null) 
            {
                GetProperties();
            }
        }
        public override void OnInspectorGUI() 
        {
            _so.Update();

            DrawMenu();

            if (GUI.changed) 
            {
                EditorUtility.SetDirty(_myTarget);
            }

            _so.ApplyModifiedProperties();
        }
        #endregion

        private void GetProperties() 
        {
            _myTarget = (LightmapManager)target;
            _so = new SerializedObject(_myTarget);

            Renderers = _so.FindProperty("_renderers");
            LightmapsTextures = _so.FindProperty("_lightmapTextures");
        }
        
        private void DrawMenu() 
        {
            EditorGUILayout.BeginVertical("Box");

            DrawMenu_Inputs();

            EditorGUILayout.Space();
            DrawMenu_ClearAll();

            DrawButton_Getters();

            EditorGUILayout.EndVertical();

            EditorGUILayout.BeginVertical("Box");

            DrawButton();

            EditorGUILayout.EndVertical();
        }

        #region MENUS
        private void DrawMenu_Inputs() {
            EditorGUILayout.PropertyField(Renderers, true);
            EditorGUILayout.PropertyField(LightmapsTextures, true);
        }

        private void DrawButton() {
            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Apply lightMaps") && _myTarget != null) {
                _myTarget.Apply();
            }

            if (GUILayout.Button("Clear") && _myTarget != null) {
                _myTarget.ClearData();
            }

            EditorGUILayout.EndHorizontal();
        }

        private void DrawButton_Getters() {
            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Get Renderers In Group") && _myTarget != null) {
                _myTarget.GetRenderers();
                _myTarget.SetLightMapDataValues();
            }

            if (GUILayout.Button("Get Textures") && _myTarget != null) {
                _myTarget.GetLightmapTextures();
            }

            EditorGUILayout.EndHorizontal();
        }

        private void DrawMenu_ClearAll() {
            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Clear ALL") && _myTarget != null) {
                _myTarget.ClearAllLightmapDataContainers();
            }

            EditorGUILayout.EndHorizontal();
        }
        #endregion
    }
}