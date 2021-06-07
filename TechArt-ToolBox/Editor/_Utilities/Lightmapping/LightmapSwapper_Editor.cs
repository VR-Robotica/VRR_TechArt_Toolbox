using UnityEditor;
using UnityEngine;

namespace VRR.TechArtTools.Lightmapping
{
    [CustomEditor(typeof(LightmapSwapper))]
    public class LightmapSwapper_Editor : UnityEditor.Editor
    {
        LightmapSwapper _myTarget;
        SerializedObject _so;

        SerializedProperty Set1;
        SerializedProperty Set2;


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
            _myTarget = (LightmapSwapper)target;
            _so     = new SerializedObject(_myTarget);

            Set1    = _so.FindProperty("_set1");
            Set2    = _so.FindProperty("_set2");
        }

        private void DrawMenu()
        {
            EditorGUILayout.BeginVertical("Box");

            DrawMenu_Inputs();

            EditorGUILayout.EndVertical();

            EditorGUILayout.BeginVertical("Box");

            DrawButton();

            EditorGUILayout.EndVertical();
        }

        #region MENU
        private void DrawMenu_Inputs()
        {
            EditorGUILayout.PropertyField(Set1, true);
            EditorGUILayout.PropertyField(Set2, true);
        }

        private void DrawButton()
        {
            EditorGUILayout.BeginHorizontal();

            if (GUILayout.Button("Set1 - Daylight") && _myTarget != null)
            {
                _myTarget.SwapDaylight();
            }

            if (GUILayout.Button("Set2 - Nightlight") && _myTarget != null)
            {
                _myTarget.SwapNightlight();
            }

            EditorGUILayout.EndHorizontal();
        }
        #endregion
    }
}