using UnityEngine;
using UnityEditor;

namespace VRR.Avatar.Emote
{
    [CustomEditor(typeof(FaceEmote_Manager))]
    public class Editor_FaceEmote_Manager : Editor
    {
        private FaceEmote_Manager _myTarget;
        private SerializedObject _so;

        private SerializedProperty SkinnedMesh;
        private SerializedProperty EmoteController;
        private SerializedProperty AnimationSpeed;
        private SerializedProperty EmoteLibrary;

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

            Draw_Menu();

            if (GUI.changed)
            {
                EditorUtility.SetDirty(_myTarget);
            }

            _so.ApplyModifiedProperties();
        }

        private void GetProperties()
        {
            _myTarget       = (FaceEmote_Manager)target;
            _so             = new SerializedObject(_myTarget);

            SkinnedMesh     = _so.FindProperty("SkinnedMesh");
            EmoteController = _so.FindProperty("EmoteController");
            AnimationSpeed  = _so.FindProperty("AnimationSpeed");
            EmoteLibrary    = _so.FindProperty("EmoteLibrary");

        }

        private void Draw_Menu()
        {
            if (!_myTarget.IsReady)
            {
                EditorGUILayout.BeginVertical("Box");

                EditorGUILayout.PropertyField(SkinnedMesh,      new GUIContent("Skined Mesh:"));
                EditorGUILayout.PropertyField(EmoteController,  new GUIContent("Emote Controller:"));
                EditorGUILayout.PropertyField(AnimationSpeed,   new GUIContent("Animation Speed:"));
                EditorGUILayout.PropertyField(EmoteLibrary,     new GUIContent("Emote Library:"), true);

                EditorGUILayout.EndVertical();
            }
            else
            {
                EditorGUILayout.Space();

                _myTarget.FaceEmote = (FaceEmote_Types)EditorGUILayout.EnumPopup("Set Emote To: ", _myTarget.FaceEmote);

                EditorGUILayout.Space();
            }
        }
    }
}