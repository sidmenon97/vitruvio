﻿// Copyright © 2017-2020 Esri R&D Center Zurich. All rights reserved.

using System;
using UnrealBuildTool;
using System.Collections.Generic;
using System.IO;
using Tools.DotNETCommon;

public class UnrealGeometryEncoderTarget : TargetRules
{
	public UnrealGeometryEncoderTarget(TargetInfo Target)
		: base(Target)
	{
		Type = TargetType.Program;
		SolutionDirectory = "Programs";

		LaunchModuleName = "UnrealGeometryEncoder";
		ExeBinariesSubFolder = "UnrealGeometryEncoder";

		LinkType = TargetLinkType.Monolithic;
		bShouldCompileAsDLL = true;

		bBuildDeveloperTools = false;
		bUseMallocProfiler = false;
		bBuildWithEditorOnlyData = true;
		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;
		bCompileAgainstApplicationCore = false;
		bCompileICU = false;
		bUsesSlate = false;
		bDisableDebugInfo = true;
		bUsePDBFiles = true;
		bHasExports = true;
		bIsBuildingConsoleApplication = true;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			AddWindowsPreAndPostBuildSteps(Target);
		}
	}

	void AddWindowsPreAndPostBuildSteps(TargetInfo Target)
	{
		string VitruvioPath = string.Empty;
			
		// Check if Vitruvio Plugin is installed
		foreach (FileReference Plugin in UnrealBuildTool.Plugins.EnumeratePlugins(Target.ProjectFile))
		{
			if (Plugin.FullName.Contains("Vitruvio"))
			{
				VitruvioPath = Path.GetDirectoryName(Plugin.FullName);
			}
		}

		string ProjectPath = Path.GetDirectoryName(Target.ProjectFile.FullName);

		if (ProjectPath == null)
		{
			throw new InvalidOperationException("Project Path is null");
		}
		
		string BinaryFolder = Path.Combine(ProjectPath, "Binaries", "Win64", "UnrealGeometryEncoder");
		string SourceIncludeFolder = Path.Combine(ProjectPath, "Source", "UnrealGeometryEncoder", "Public");
		
		string AllBinaryFolderFiles = Path.Combine(BinaryFolder, "*.*");
		
		// We want to delete all old encoder libraries because there might be old builds with different build settings
		PreBuildSteps.Add(string.Format("echo deleting old encoder libraries \"{0}\"", AllBinaryFolderFiles));
		PreBuildSteps.Add(string.Format("del /f /q \"{0}\"", AllBinaryFolderFiles));
		
		// If Vitruvio is installed, copy the include and library files into the ThirdParty folder of Vitruvio
		if (!string.IsNullOrEmpty(VitruvioPath))
		{
			string VitruvioEncoderLib = Path.Combine(VitruvioPath, "Source", "ThirdParty", "UnrealGeometryEncoderLib");
			string LibFolder = Path.Combine(VitruvioEncoderLib, "lib", "Win64", "Release");

			string SrcPath = AllBinaryFolderFiles;
			string DestPath = LibFolder;
			
			PostBuildSteps.Add(string.Format("echo Copying \"{0}\" to \"{1}\"", SrcPath, DestPath));
			PostBuildSteps.Add(string.Format("xcopy \"{0}\" \"{1}\" /R /Y /S", SrcPath, DestPath));

			SrcPath = Path.Combine(SourceIncludeFolder, "*.*");
			DestPath = Path.Combine(VitruvioEncoderLib, "include");
			
			PostBuildSteps.Add(string.Format("echo Copying \"{0}\" to \"{1}\"", SrcPath, DestPath));
			PostBuildSteps.Add(string.Format("xcopy \"{0}\" \"{1}\" /R /Y /S", SrcPath, DestPath));
		}
	}
}