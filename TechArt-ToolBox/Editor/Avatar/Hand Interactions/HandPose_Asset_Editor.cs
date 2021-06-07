using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEditor;

namespace VRR.Avatar.Hands
{
    [CustomEditor(typeof(HandPose_Asset))]
    public class HandPose_Asset_Editor : Editor
    {
        HandPose_Asset _myTarget;
        SerializedObject _so;

        SerializedProperty PoseType;
        SerializedProperty FingerBases;
        SerializedProperty FingerTips;
        SerializedProperty ThumbBase;
        SerializedProperty ThumbTip;

        private void OnEnable()
        {
            if (_myTarget == null || _so == null)
            {
                GetProperties();
            }
        }

        private void GetProperties()
        {
            _myTarget = (HandPose_Asset)target;

            _so = new SerializedObject(_myTarget);

            PoseType = _so.FindProperty("PoseType");

            FingerBases = _so.FindProperty("FingerBaseCurlValues");
            FingerTips = _so.FindProperty("FingerTipCurlValues");

            ThumbBase = _so.FindProperty("ThumbBaseCurlValues");
            ThumbTip = _so.FindProperty("ThumbTipCurlValues");
        }

        public override void OnInspectorGUI()
        {
            _so.Update();

            draw_menu();

            if (GUI.changed)
            {
                EditorUtility.SetDirty(_myTarget);
            }

            _so.ApplyModifiedProperties();
        }

        private void draw_menu()
        {
            EditorGUILayout.BeginVertical("Box");

            EditorGUILayout.PropertyField(PoseType,     new GUIContent("Hand Pose Type:"));

            EditorGUILayout.Space();

            EditorGUILayout.BeginVertical("Box");
            EditorGUILayout.PropertyField(FingerBases, true);
            EditorGUILayout.PropertyField(FingerTips, true);
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space();

            EditorGUILayout.BeginVertical("Box");
            EditorGUILayout.PropertyField(ThumbBase, true);
            EditorGUILayout.PropertyField(ThumbTip, true);
            EditorGUILayout.EndVertical();

            EditorGUILayout.EndVertical();
        }
    }
}