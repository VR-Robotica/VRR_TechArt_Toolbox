using UnityEngine;
using UnityEditor;

namespace VRR.Avatar.Hands
{
    [CustomEditor(typeof(HandPose_Controller))]
    public class HandPoseController_Editor : Editor
    {
        private HandPose_Controller _myTarget;

        private bool _needsConstantRepaint;

        private void OnEnable()
        { 
            _myTarget = (HandPose_Controller)target;

            _needsConstantRepaint = true;
        }

        private void OnDisable()
        {
            _needsConstantRepaint = false;
        }

        public override bool RequiresConstantRepaint()
        {
            return _needsConstantRepaint;
        }

        public override void OnInspectorGUI()
        {
            if (_myTarget == null){  return; }

            EditorGUI.BeginChangeCheck();

            draw_menu();

            if (GUI.changed) { EditorUtility.SetDirty(_myTarget);}

            EditorGUI.EndChangeCheck();
        }

        private void draw_menu()
        {
            if (!Application.isPlaying)
            {
                DrawDefaultInspector();
            }
            else
            {
                EditorGUILayout.LabelField("Finger Curl Controls", EditorStyles.boldLabel);

                EditorGUILayout.BeginVertical("Box");

                for (int i = 0; i < _myTarget.Hand.GetFingerLength(); i++)
                {
                    _myTarget.FingerBaseCurlAmounts[i] = EditorGUILayout.Slider("Base Curl " + _myTarget.Hand.GetFingerName(i) + ": ", _myTarget.FingerBaseCurlAmounts[i], -1.0f, 1.0f);
                    _myTarget.FingerTipCurlAmounts[i] = EditorGUILayout.Slider("Tip Curl: ", _myTarget.FingerTipCurlAmounts[i], 0.0f, 1.0f);

                    EditorGUILayout.Space();
                }

                // Finger Spread not yet set up
                //_myTarget.FingerSpreadAmount = EditorGUILayout.Slider("Finger Spread: ", _myTarget.FingerSpreadAmount, 0.0f, 1.0f);

                EditorGUILayout.EndVertical();

                EditorGUILayout.Space();

                EditorGUILayout.LabelField("Thumb Curl Controls", EditorStyles.boldLabel);

                EditorGUILayout.BeginVertical("Box");

                for (int i = 0; i < _myTarget.Hand.GetThumbLength(); i++)
                {
                    _myTarget.ThumbBaseCurlAmounts[i] = EditorGUILayout.Slider("Base Curl " + _myTarget.Hand.GetThumbName(i) + ": ", _myTarget.ThumbBaseCurlAmounts[i], -1.0f, 1.0f);
                    _myTarget.ThumbTipCurlAmounts[i] = EditorGUILayout.Slider("Tip Curl: ", _myTarget.ThumbTipCurlAmounts[i], 0.0f, 1.0f);
                    EditorGUILayout.Space();
                    // _myTarget.SpreadFingerAmount = EditorGUILayout.Slider("Finger Spread: ", _myTarget.SpreadFingerAmount, 0.0f, 1.0f);
                }

                EditorGUILayout.Space();

                EditorGUILayout.EndVertical();

            }
        }
    }
}