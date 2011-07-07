<?php

// $Id$

/*
 * @file
 *
 */
module_load_include('inc', 'fedora_repository', 'api/fedora_item');
module_load_include('inc', 'fedora_repository', 'ContentModel');
module_load_include('inc', 'xml_form_api', 'XMLForm');

/**
 *
 */
class EditObjectMetadataForm {

  /**
   * Get Content Models for the form.
   * 
   * @param string $collection_pid
   * 
   * @return array
   */
  public function getPotentialContentModels($collection_pid) {
    if (($collection_policy = CollectionPolicy::loadFromCollection($collection_pid)) === FALSE) {
      drupal_set_message(t('Unable to load collection policy \'' . $collection_pid . '\'.'));
      return FALSE;
    }
    if (!($content_models = $collection_policy->getContentModels())) {
      drupal_set_message(t('No content models associated with this collection: !collection_label. Please contact your administrator.', array('!collection_label' => $collection_label)), 'error');
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
   * Create page one.
   * 
   * @param string $pid
   * @param string $dsid
   * @param SoapClient $client
   * @param array $form_state
   * 
   * @return array
   */
  public function createPageOne($pid, $client, array &$form_state) {
    $content_model = ContentModel::loadFromObject($pid);
    $form_names = islandora_content_model_get_form_names($content_model->pid);
    if (empty($form_names)) {
      throw new Exception('Couldn\'t Create Page One of the Edit Form.');
    }
    return array(
      'indicator' => array(
        '#type' => 'fieldset',
        '#title' => t('Choose edit form.'),
        'forms' => array(
          '#type' => 'select',
          '#title' => t('Forms'),
          '#options' => $form_names,
          '#description' => t('Select the form to populate the metadata of the new object.')
        )
      ),
      'submit' => array(
        '#type' => 'submit',
        '#submit' => array('fedora_repository_edit_qdc_form_submit'),
        '#value' => 'Next'
      )
    );
  }

  /**
   * Create page two.
   * 
   * @param string $pid
   * @param string $dsid
   * @param SoapClient $client
   * @param array $form_state
   * 
   * @return array
   */
  public function createPageTwo($pid, $client, array &$form_state) {
    $xml_form = new XMLForm($form_state);
    if (!$xml_form->isInitialized()) {
      module_load_include('inc', 'xml_form_api', 'XMLFormDefinition');
      module_load_include('inc', 'xml_form_builder', 'FormBuilder');
      $form_name = $form_state['values']['forms'];
      $definition = FormBuilder::GetFormDefinition($form_name);
      $form = XMLFormDefinition::GetDrupalForm($definition);
      $properties = XMLFormDefinition::GetFormProperties($definition);
      $content_model = ContentModel::loadFromObject($pid);
      $dsid = islandora_content_model_get_dsid($content_model->pid, $form_name);
      $params = array('pid' => $pid, 'dsID' => $dsid, 'asOfDateTime' => "");
      try {
        $object = $client->__soapCAll('getDatastreamDissemination', array('parameters' => $params));
      } catch (Exception $e) {
        return array();
      }
      $xml = trim($object->dissemination->stream);
      $document = new XMLDocument($properties['document']['root'], $properties['document']['namespaces'], $properties['document']['schema'], $xml);
      $xml_form->initialize($form, $document);
    }
    $output = $xml_form->toArray();
    $output['forms'] = array(
      '#type' => 'hidden',
      '#value' => $form_state['values']['forms'],
    );
    $output['pid'] = array(
      '#type' => 'hidden',
      '#value' => $pid,
    );
    $output['dsid'] = array(
      '#type' => 'hidden',
      '#value' => isset($form_state['values']['dsid']) ? $form_state['values']['dsid'] : $dsid,
    );
    return $output;
  }

  /**
   * Create forms.
   * 
   * @param string $pid
   * @param string $dsid
   * @param SoapClient $client
   * @param array $form_state
   * 
   * @return array
   */
  public function create($pid, $client, array &$form_state) {
    $page = $form_state['storage']['step'] = empty($form_state['storage']['step']) ? 1 : $form_state['storage']['step'];
    if ($page == 1) {
      $form_state['storage']['xml'] = true;
      return $this->createPageOne($pid, $client, $form_state);
    }
    else if ($form_state['storage']['xml']) {
      return $this->createPageTwo($pid, $client, $form_state);
    }
    else {
      throw new Exception('Couldn\'t Create the Edit Form.');
    }
  }

  /**
   * Submit.
   * 
   * @param array $form
   * @param array $form_state 
   */
  public function submit(array &$form, array &$form_state) {
    global $base_url;
    $pid = $form_state['values']['pid'];
    $dsid = $form_state['values']['dsid'];
    // @todo: Set up the field that becomes the label.
    /**
     * $object = new Fedora_Item($pid);
     * $object->modify_object($form['#post']['form_builder'][0]); // set label
     */
    $xml_form = new XMLForm($form_state);
    $doc = $xml_form->submit($form, $form_state);
    $document = $doc->document;
    $this->modifyDatastream($document, $pid, $dsid, "$dsid Record", 'text/xml');
    // @todo: Perform Transform
    /**
     * $transform = drupal_get_path('module', 'islandora_form_builder') . '/transforms/mods_to_dc.xsl'; // TODO add transform to content modeler ...
     * $dublin_core = $this->transformDocumentIntoDublinCore($document, $transform);
     * $dublin_core->version = "1.0";
     * $dublin_core->encoding = "UTF-8";
     * $dublin_core->formatOutput = TRUE;
     * $this->modifyDatastream($dublin_core, 'DC', "Dublin Core Record", 'text/xml');
     */
    $form_state['storage'] = NULL;
    $form_state['redirect'] = $base_url . '/fedora/repository/' . $pid;
  }

  /**
   * Test if a data stream is managed or not.
   *
   * @param $dsid
   *   Data stream identifier.
   *
   * @return
   *  TRUE if the data stream specified by $disd is managed, FALSE otherwise.
   */
  private function isDatastreamManaged($pid, $dsid) {
    $fedora_item = new Fedora_Item($pid);
    $datastream = $fedora_item->get_datastream($dsid);
    return $datastream->controlGroup == 'M';
  }

  /**
   * Overwrites a managed data stream by reference.
   *
   * Only works for managed data streams. Creates a temp file to be used as the
   * reference
   *
   * @param $dom
   *   XML document that will replace the data stream.
   * @param $dsid
   *   Data stream identifier.
   * @param $label
   *   Data stream label.
   * @param $mime_type
   *   Data stream mime_type.
   */
  private function modifyManagedDatastream(DOMDocument $dom, $pid, $dsid, $label, $mime_type) {
    $temp_file_dir = file_directory_temp();
    $temp_file_path = file_create_filename("datastream.xml", $temp_file_dir);
    if ($dom->save($temp_file_path)) {
      $temp_file_url = file_create_url($temp_file_path);
      $fedora_item = new Fedora_Item($pid);
      if ($fedora_item->modify_datastream_by_reference($temp_file_url, $dsid, $label, $mime_type) !== NULL) {
        drupal_set_message(t('Successfully updated %dsid datastream for object %pid', array('%pid' => $pid, '%dsid' => $dsid)));
      }
    }
  }

  /**
   * Overwrites a inlined data stream by value.
   *
   * Only works for inlined data streams.
   *
   * @param $dom
   *   XML document that will replace the data stream.
   * @param $dsid
   *   Data stream identifier.
   * @param $label
   *   Data stream label.
   * @param $mime_type
   *   Data stream mime_type.
   */
  private function modifyInlineDatastream(DOMDocument $dom, $pid, $dsid, $label, $mime_type) {
    $fedora_item = new Fedora_Item($pid);
    if ($fedora_item->modify_datastream_by_value($dom->saveXML(), $dsid, $label, $mime_type) !== NULL) {
      drupal_set_message(t('Successfully updated %dsid datastream for object %pid', array('%pid' => $pid, '%dsid' => $dsid)));
    }
  }

  /**
   * Overwrites a data stream.
   *
   * Overwrites the given datastream by value for inline datastreams and by
   * reference for managed datastreams.
   *
   * @param $dom
   *   XML document that will replace the data stream.
   * @param $dsid
   *   Data stream identifier.
   * @param $label
   *   Data stream label.
   * @param $mime_type
   *   Data stream mime_type.
   */
  private function modifyDatastream($dom, $pid, $dsid, $label, $mime_type) {
    if ($this->isDatastreamManaged($pid, $dsid)) {
      $this->modifyManagedDatastream($dom, $pid, $dsid, $label, $mime_type);
    }
    else {
      $this->modifyInlineDatastream($dom, $pid, $dsid, $label, $mime_type);
    }
  }

}