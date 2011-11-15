<?php

/**
 * @file
 * 
 * Collection of functions related to Batch ingests.
 */

/**
 * Performs a bulk ingestion.
 * 
 * This is not meant to be called from drupal_get_form, 
 * this is a sub-form that part of the content_model_viewer_ingest_metadata
 * 
 * @param array $form_state
 *   The Drupal form state.
 * @param string $collection_pid 
 *   The pid of the collection we will ingest into.
 * 
 * @return array
 *   The Drupal form.
 */
function scholar_bulk_ingest_form(array &$form_state, $collection_pid) {
  $potential_models = scholar_bulk_ingest_get_potential_models($collection_pid);
  reset($potential_models);
  $identifier = key($potential_models);
  $name = current($potential_models);
  $selected_model = isset($form_state['values']['models']) ? $form_state['values']['models'] : $identifier;
  $form = array(
    'bulk_ingest' => array(
      '#type' => 'fieldset',
      '#title' => t('Ingest digital objects generated by RIS/EndNote into collection_pid Step #1', array('collection_pid' => $collection_pid)),
      'file' => array(
        '#type' => 'file',
        '#title' => t('Upload "RIS" or "EndNote XML" Document'),
        '#description' => t('A RIS or EndNote XML document that will generate multiple digital objects on ingest. One for each record within the RIS or EndNote XML document.'),
        '#element_validate' => array('scholar_bulk_ingest_file_upload_validate'),
      ),
      'content_model_pid' => array(
        '#type' => 'select',
        '#title' => t('Content models available'),
        '#options' => $potential_models,
        '#default_value' => $selected_model,
        '#description' => t('Content models define datastream composition, relationships between this and other content models, and the mandatory behaviors associated with each digital object.<br /> Additional information may be found <a href="https://wiki.duraspace.org/display/FEDORACREATE/Content+Models+Overview">here.</a> '),
      ),
      'submit' => array(
        '#type' => 'submit',
        '#executes_submit_callback' => TRUE,
        '#submit' => array('scholar_bulk_ingest_form_submit'),
        '#value' => t('Ingest')
      ),
    )
  );
  return $form;
}

/**
 * Get Potential Models that can be part of the given collection.
 * 
 * @param string $collection_pid
 *   The pid of the collection.
 * @return array
 *   Where the key is the Content Model PID and the value is the human readable name for the Content Model.
 */
function scholar_bulk_ingest_get_potential_models($collection_pid) {
  if (($collection_policy = CollectionPolicy::loadFromCollection($collection_pid)) === FALSE) {
    drupal_set_message(t('Unable to load collection policy \'' . $collection_pid . '\'.'));
    return FALSE;
  }
  if (!($content_models = $collection_policy->getContentModels())) {
    drupal_set_message(t('No content models associated with this collection: !collection_pid. Please contact your administrator.', array('!collection_pid' => $collection_pid)), 'error');
    return FALSE;
  }
  $potential_models = array();
  foreach ($content_models as $content_model) {
    $identifier = $content_model->getIdentifier();
    $name = $content_model->name;
    $potential_models["$identifier"] = "$name";
  }
  return $potential_models;
}

/**
 * Over writes the default 'content_model_viewer_ingest_metadata_form_validate' function.
 * 
 * @param array $form
 *   The Drupal form.
 * @param array $form_state 
 *   The Drupal form state.
 */
function scholar_bulk_ingest_form_validate(array &$form, array &$form_state) {
  $bulk_ingest_clicked = $form_state['clicked_button']['#value'] == t('Ingest');
  if (!$bulk_ingest_clicked) {
    if ($form_state['storage']['step'] == 1) {
      $form_state['storage']['step']++;
      $form_state['rebuild'] = TRUE;
    }
    else {
      module_load_include('inc', 'xml_form_api', 'XMLForm');
      $xml_form = new XMLForm($form_state);
      $xml_form->validate($form, $form_state);
    }
  }
}

/**
 * Makes sure the file was uploaded and is of the right type.
 * 
 * @param array $element
 *   The file upload field
 * @param array $form_state 
 *   The Drupal form state.
 */
function scholar_bulk_ingest_file_upload_validate(array $element, array &$form_state) {
  $bulk_ingest_clicked = $form_state['clicked_button']['#value'] == t('Ingest');
  $file_uploaded = isset($_FILES['files']['error']['file']) && $_FILES['files']['error']['file'] == 0;
  if ($bulk_ingest_clicked) {
    if (!$file_uploaded) {
      form_error($element, t('You must upload a "RIS" or "EndNote XML" document.'));
    }
    else { // Only support for one file.
      $mime_type = $_FILES['files']['type']['file'];
      $valid_types = array('text/xml', 'text/plain', 'application/x-research-info-systems');
      if (array_search($mime_type, $valid_types) === FALSE) {
        form_error($element, t('The upload file is not the correct type. You must upload a "RIS" or "EndNote XML" document.'));
      }
    }
  }
}

/**
 * Bulk Ingest object.
 * 
 * @param array $form
 *   The Drupal form.
 * @param array $form_state 
 *   The Drupal form state.
 */
function scholar_bulk_ingest_form_submit(array &$form, array &$form_state) {
  $collection_pid = $form_state['values']['collection_pid'];
  $content_model_pid = ContentModel::getPidFromIdentifier($form_state['values']['content_model_pid']);
  $mime_type = $_FILES['files']['type']['file'];
  $file = file_save_upload('file');
  $filename = $file->filepath;
  $function = ($mime_type == 'text/xml') ? 'scholar_batch_ingest_endnote_document' : 'scholar_batch_ingest_ris_document';
  $operations = array(
    array($function, array($filename, $collection_pid, $content_model_pid))
  );
  $batch = array(
    'title' => t('Ingesting Files'),
    'operations' => $operations,
    'file' => drupal_get_path('module', 'scholar') . '/Batch.inc',
  );
  batch_set($batch);
  unset($form_state['storage']); // Return to the viewer.  
}

/**
 * Ingests multiple objects one for each record within the given EndNote XML document.
 * 
 * @param string $filename
 *   The name of the EndNote XML file to ingest.
 * @param type $collection_pid
 *   The pid of the collection to ingest into.
 * @param type $content_model_pid
 *   The content model to assign to the new pid.
 * @param array $context 
 *   The drupal batch context.
 */
function scholar_batch_ingest_endnote_document($filename, $collection_pid, $content_model_pid, array &$context) {
  module_load_include('inc', 'bibutils', 'Batch');
  module_load_include('inc', 'bibutils', 'Bibutils');
  if (empty($context['sandbox'])) {
    $context['sandbox']['progress'] = 0;
    $context['sandbox']['total'] = bibutils_batch_get_number_endnote_records($filename);
  }
  $temp_file_dir = file_directory_temp();
  $mods_filename = file_create_filename("mods.xml", $temp_file_dir);
  $endnote_filename = file_create_filename("endnote.xml", $temp_file_dir);
  $record = bibutils_batch_get_endnote_record($filename); // Get Single EndNote XML Record
  $record->save($endnote_filename);
  if (Bibutils::Convert($endnote_filename, 'EndNoteXML', $mods_filename, 'MODS')) {
    scholar_batch_ingest_mods($mods_filename, $collection_pid, $content_model_pid, array('ENDNOTE' => $endnote_filename));
  }
  $context['sandbox']['progress']++;
  if ($context['sandbox']['progress'] != $context['sandbox']['total']) {
    $context['finished'] = $context['sandbox']['progress'] / $context['sandbox']['total'];
  }
}

/**
 * Batch ingest the RIS document.
 * 
 * @param string $filename
 *   The name of the EndNote XML file to ingest.
 * @param type $collection_pid
 *   The pid of the collection to ingest into.
 * @param type $content_model_pid
 *   The content model to assign to the new pid.
 * @param array $context 
 *   The drupal batch context.
 */
function scholar_batch_ingest_ris_document($filename, $collection_pid, $content_model_pid, array &$context) {
  module_load_include('inc', 'bibutils', 'Batch');
  module_load_include('inc', 'bibutils', 'Bibutils');
  if (empty($context['sandbox'])) {
    $context['sandbox']['progress'] = 0;
    $context['sandbox']['total'] = bibutils_batch_get_number_ris_records($filename);
  }
  $temp_file_dir = file_directory_temp();
  $mods_filename = file_create_filename("mods.xml", $temp_file_dir);
  $ris_filename = file_create_filename("ris.txt", $temp_file_dir);
  $record = bibutils_batch_get_ris_record($filename); // Get Single EndNote XML Record
  $file = fopen($ris_filename, 'w');
  fwrite($file, $record);
  fclose($file);
  if (Bibutils::Convert($ris_filename, 'RIS', $mods_filename, 'MODS')) {
    scholar_batch_ingest_mods($mods_filename, $collection_pid, $content_model_pid, array('RIS' => $ris_filename));
  }
  // Remove the files.
  unlink($ris_filename);
  unlink($mods_filename);
  $context['sandbox']['progress']++;
  if ($context['sandbox']['progress'] != $context['sandbox']['total']) {
    $context['finished'] = $context['sandbox']['progress'] / $context['sandbox']['total'];
  }
}

/**
 * todo add comments
 * 
 * @param string $filename
 * @param string $collection_pid
 * @param string $content_model_pid
 * @param array $datastreams 
 */
function scholar_batch_ingest_mods($filename, $collection_pid, $content_model_pid, array $datastreams) {
  module_load_include('inc', 'islandora_content_model_forms', 'FOXML');
  module_load_include('inc', 'fedora_repository', 'CollectionPolicy');
  $collection_policy = CollectionPolicy::loadFromCollection($collection_pid);
  if ($collection_policy !== FALSE) {
    $document = new DOMDocument();
    $document->load($filename);
    $label = scholar_batch_ingest_mods_guess_title($document); // Find a best guess solution... to pull from the mods.
    $pid = $collection_policy->getNextPid('ISLANDORACM');
    $dsid = 'MODS';
    $relationship = $collection_policy->getRelationship();
    $ingest_file_location = $datastreams;
    $transform = drupal_get_path('module', 'scholar') . '/xsl/mods_to_dc.xsl';
    $state = 'A';
    $foxml = new FOXML($label, $pid, $dsid, $content_model_pid, $collection_pid, $relationship, $ingest_file_location, $document, $transform, $state);
    $foxml->ingest();
  }
}

/**
 * Guess the title for the object generated from mods.
 * 
 * @param DOMDocument $mods
 *   The MODS document in which to extract the title from.
 * 
 * @return string
 *   The extracted title or a default title.
 */
function scholar_batch_ingest_mods_guess_title(DOMDocument $mods) {
  $default_title = t('Default Title (Please Correct)');
  $xpath = new DOMXPath($mods);
  $xpath->registerNamespace('mods', 'http://www.loc.gov/mods/v3');
  $results = $xpath->query('/mods:mods/mods:titleInfo[not(@type)]/mods:title[1]'); // Ingnore extras.
  if ($results->length == 1) {
    $title = $results->item(0);
    return $title->textContent;
  }
  $results = $xpath->query('/mods:mods/mods:titleInfo/mods:title[1]'); // Ingnore extras, can be abbr or whatever.
  if ($results->length > 0) { // Can be more than one.
    $title = $results->item(0); // Get first
    return $title->textContent;
  }
  return $default_title;
}